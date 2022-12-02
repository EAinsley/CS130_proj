#include "swap.h"
#include "bitmap.h"
#include "devices/block.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "userprog/syscall.h" // for err_exit

static struct block *swap_block;
static char *bitmap_buf[512];
static struct bitmap *used_slots;
static struct lock swap_lock;
static const uint32_t SEC_PER_FRAME = PGSIZE / BLOCK_SECTOR_SIZE;

#define SWAP_CRITICAL                                                         \
  for (int i = (lock_acquire (&swap_lock), 0); i < 1;                         \
       lock_release (&swap_lock), i++)

/* find a free slot in the bitmap and set occupied bit */
static swap_idx find_free_slot (void);

void
vm_swap_init ()
{
  swap_block = block_get_role (BLOCK_SWAP);
  ASSERT (swap_block != NULL);

  uint32_t swap_slots = block_size (swap_block) / SEC_PER_FRAME;
  used_slots = bitmap_create_in_buf (swap_slots, (void *)bitmap_buf,
                                     sizeof (bitmap_buf));

  lock_init (&swap_lock);
}

swap_idx
vm_swap_save (void *page)
{
  // should never evict pages for kernel.
  ASSERT (page >= PHYS_BASE);

  SWAP_CRITICAL
  {
    swap_idx idx = find_free_slot ();
    bitmap_mark (used_slots, idx);
    for (uint32_t i = 0, sec = idx * SEC_PER_FRAME; i < SEC_PER_FRAME;
         i++, sec++, page += BLOCK_SECTOR_SIZE)
      {
        block_write (swap_block, sec, page);
      }
    return idx;
  }
  return (swap_idx)(-1);
}

void
vm_swap_load (void *page, swap_idx idx)
{
  // should never evict kernel page
  ASSERT (page >= PHYS_BASE);

  SWAP_CRITICAL
  {
    // must present in swap partition
    ASSERT (bitmap_test (used_slots, idx));

    bitmap_reset (used_slots, idx);
    for (uint32_t i = 0, sec = idx * SEC_PER_FRAME; i < SEC_PER_FRAME;
         i++, sec++, page += BLOCK_SECTOR_SIZE)
      {
        block_read (swap_block, sec, page);
      }
  }
}

void
vm_swap_discard (swap_idx idx)
{
  SWAP_CRITICAL
  {
    // must present in swap partition
    ASSERT (bitmap_test (used_slots, idx));
    bitmap_reset (used_slots, idx);
  }
}

///
/// helper functions
///

static swap_idx
find_free_slot ()
{
  swap_idx idx = bitmap_scan (used_slots, 0, 1, false);
  ASSERT (idx != BITMAP_ERROR);
  return idx;
}