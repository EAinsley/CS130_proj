#include "buffer_cache.h"
#include "devices/timer.h"
#include "lib/debug.h"
#include "lib/string.h"
#include "list.h"
#include "threads/synch.h"
#include "threads/thread.h"

#define BUFFER_CACHE_SIZE 64
static struct buffer_cache_node cache[BUFFER_CACHE_SIZE];
static struct lock buffer_cache_lock;
static size_t clock_pointer;
static size_t cache_size; // the number of nodes in the cache.
static bool is_closing;   // closing signal

static struct buffer_cache_node *
buffer_cache_find_sector (block_sector_t sector);
static struct buffer_cache_node *buffer_cache_find_empty (void);
static size_t buffer_cache_find_victim (void);
static void buffer_cache_flush (size_t i);
static void write_behind (void);

void
buffer_cache_init ()
{
  lock_init (&buffer_cache_lock);
  memset (cache, 0, sizeof (cache));
  clock_pointer = 0;
  cache_size = 0;
  is_closing = false;
  thread_create ("write-behind", PRI_DEFAULT, write_behind, NULL);
}

/* Read a block into the cache. Then read the data from the cache to dest. The
 * data was specified by the offset and length*/
void
buffer_cache_read (block_sector_t sector, void *dest, off_t offset,
                   off_t length)
{
  ASSERT (offset + length <= BLOCK_SECTOR_SIZE);
  lock_acquire (&buffer_cache_lock);
  // Try to find sector
  struct buffer_cache_node *node = buffer_cache_find_sector (sector);
  if (!node)
    {
      // On cache miss: find a new slot and read from disk
      node = buffer_cache_find_empty ();
      ASSERT (!node->in_use);
      // Set the node
      block_read (fs_device, sector, node->buffer);
      node->sector = sector;
      node->in_use = true;
      node->dirty = false;
      // Add size
      cache_size++;
    }
  node->access = true;
  // read the data
  memcpy (dest, node->buffer + offset, length);
  lock_release (&buffer_cache_lock);
}

/* fetch a sector into the cache*/
void
buffer_cache_prefetch (block_sector_t sector)
{
  lock_acquire (&buffer_cache_lock);
  struct buffer_cache_node *node = buffer_cache_find_sector (sector);
  if (!node)
    {
      node = buffer_cache_find_empty ();
      block_read (fs_device, sector, node->buffer);
      node->sector = sector;
      node->dirty = false;
      node->in_use = true;
      cache_size++;
    }
  node->access = true;
  lock_release (&buffer_cache_lock);
}

/* Read a block into the cache. Then write data from src to the cache.*/
void
buffer_cache_write (block_sector_t sector, const void *src, off_t offset,
                    off_t length)
{
  ASSERT (offset + length <= BLOCK_SECTOR_SIZE);
  lock_acquire (&buffer_cache_lock);
  // Try to find the sector
  struct buffer_cache_node *node = buffer_cache_find_sector (sector);
  if (!node)
    {
      // On cache miss: find a new slot and read from disk
      node = buffer_cache_find_empty ();
      block_read (fs_device, sector, node->buffer);
      node->sector = sector;
      node->in_use = true;
      cache_size++;
    }
  node->access = true;
  node->dirty = true;
  // write the data
  memcpy (node->buffer + offset, src, length);
  lock_release (&buffer_cache_lock);
}
void
buffer_cache_close (void)
{
  lock_acquire (&buffer_cache_lock);
  is_closing = true;
  for (int i = 0; i < BUFFER_CACHE_SIZE; i++)
    {
      if (cache[i].in_use && cache[i].dirty)
        {
          block_write (fs_device, cache[i].sector, cache[i].buffer);
          cache[i].dirty = false;
        }
    }
  lock_release (&buffer_cache_lock);
}

/* Helper functions*/

/* Find the cache node with sector. Return NULL if the node doesn't in the
 * cache.*/
static struct buffer_cache_node *
buffer_cache_find_sector (block_sector_t sector)
{
  ASSERT (lock_held_by_current_thread (&buffer_cache_lock));
  for (int i = 0; i < BUFFER_CACHE_SIZE; i++)
    {
      if (cache[i].in_use && cache[i].sector == sector)
        {
          return cache + i;
        }
    }
  return NULL;
}

/* Find an empty slot in the buffer cache*/
static struct buffer_cache_node *
buffer_cache_find_empty ()
{
  ASSERT (lock_held_by_current_thread (&buffer_cache_lock));
  int idx = 0;
  if (cache_size < BUFFER_CACHE_SIZE)
    {
      for (idx = 0; idx < BUFFER_CACHE_SIZE && cache[idx].in_use; idx++)
        ;
    }
  else
    {
      idx = buffer_cache_find_victim ();
      buffer_cache_flush (idx);
    }
  return cache + idx;
}

/* Find a victim to be evicted. If no such node, return BUFFER_CACHE_SIZE*/
static size_t
buffer_cache_find_victim ()
{
  ASSERT (lock_held_by_current_thread (&buffer_cache_lock));
  for (int i = 0; i < 2 * BUFFER_CACHE_SIZE;
       i++, clock_pointer = (clock_pointer + 1) % BUFFER_CACHE_SIZE)
    {
      // Skip empty
      if (!cache[clock_pointer].in_use)
        continue;
      // Skip accessed
      if (cache[clock_pointer].access)
        {
          cache[clock_pointer].access = false;
          continue;
        }
      return clock_pointer;
    }
  return BUFFER_CACHE_SIZE;
}

/* Flush the cache line. Write back if needed. */
static void
buffer_cache_flush (size_t idx)
{
  ASSERT (cache_size > 0);
  ASSERT (lock_held_by_current_thread (&buffer_cache_lock));
  ASSERT (idx < BUFFER_CACHE_SIZE);
  ASSERT (cache[idx].in_use);
  if (cache[idx].dirty)
    {
      // Write back
      block_write (fs_device, cache[idx].sector, cache[idx].buffer);
    }
  cache[idx].in_use = false;
  cache_size--;
}

static void
write_behind (void)
{
  while (!is_closing)
    {
      lock_acquire (&buffer_cache_lock);
      for (int i = 0; i < BUFFER_CACHE_SIZE; i++)
        {
          if (cache[i].in_use && cache[i].dirty)
            {
              block_write (fs_device, cache[i].sector, cache[i].buffer);
              // clear the dirty state
              cache[i].dirty = false;
            }
        }
      lock_release (&buffer_cache_lock);
      // sleep for two seconds
      timer_sleep (2 * TIMER_FREQ);
    }
}

#undef BUFFER_CACHE_SIZE