#ifndef VM_SWAP_H
#define VM_SWAP_H

#include <stdint.h>

/*
  swap index: used to index the blocks for a frame in swap sector
*/
typedef uint32_t swap_idx;

/*
  initialize the frame swap module:
  - bitmap
  - lock
*/
void vm_swap_init (void);

/*
  save the frame starting at `page` to SWAP,
  return a ticket to load it in the future.
*/
swap_idx vm_swap_save (void *page);

/*
  load the frame store at `swap_idx` into the frame starting at `page`
*/
void vm_swap_load (void *page, swap_idx swap_idx);

/*
  discard the frame stored in SWAP index by `swap_idx`.
  when a process exits, all the pages in SWAP should be discarded.
*/
void vm_swap_discard (swap_idx swap_idx);

#endif