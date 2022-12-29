#ifndef BUFFER_CACHE_H
#define BUFFER_CACHE_H

#include "devices/block.h"
#include "filesys/filesys.h"
struct buffer_cache_node
{
  block_sector_t sector;             // the sector index
  uint8_t buffer[BLOCK_SECTOR_SIZE]; // the buffer used to store the data
  bool dirty;                        // dirty bit
  bool access;                       // access bit for the clock eviction
  bool in_use;                       // if this cache node is in use.
};

void buffer_cache_init (void);

/* Read a block into the cache. */
void buffer_cache_read (block_sector_t sector, void *dest, off_t offset,
                        off_t length);
void buffer_cache_write (block_sector_t sector, const void *src, off_t offset,
                         off_t length);
void buffer_cache_close (void);

// TODO - Periodically writeback
// TODO - Prefetch sector

#endif // BUFFER_CACHE_H