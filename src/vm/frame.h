#ifndef VM_FRAME_H
#define VM_FRAME_H
#include "lib/kernel/hash.h"
#include "lib/kernel/list.h"
#include "threads/palloc.h"
#include "threads/thread.h"

struct vm_frame
{
  /* physical (kernel) address of this frame */
  void *phy_addr;
  /* the page that currently occupies this frame*/
  void *upage_addr;
  /* the thread who have a page mapped to this frame */
  struct thread *owner;

  /* NOTE - we may need some access control to keep the frame from being
   * evicted immediately after allocation. Not sure how to do, yet. Maybe we
   * need: */
  // /* bool pin; */

  /* hash map element, used for quick search*/
  struct hash_elem hash_elem;
  /* list element to implement LRU */
  struct list_elem list_elem;
};

void vm_frame_init (void);
void *vm_frame_allocate (enum palloc_flags flags, void *upage);
void vm_frame_free (void *kpage, bool free_resource);
void vm_frame_access (void *);
#endif // VM_FRAME_H
