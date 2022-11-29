#ifndef VM_FRAME_H
#define VM_FRAME_H
#include "debug.h"
#include "lib/kernel/hash.h"
#include "lib/kernel/list.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/syscall.h"

struct vm_frame_node
{
  /* physical (kernel) address of this frame */
  void *phy_addr;
  /* the page that currently occupies this frame*/
  /* NOTE - Not sure what data type should it be, may be changed later */
  void *upage_addr;

  /* NOTE - we may need some access control to keep the frame from being
   * evicted immediately after allocation. Not sure how to do, yet. Maybe we
   * need: */
  /* bool pin; */

  /* hash map element, used for quick search*/
  struct hash_elem hash_elem;
  /* list element to implement LRU */
  struct list_elem list_elem;
};

void vm_frame_init (void);
void *vm_frame_allocate (enum palloc_flags, void *);
void vm_frame_free (void *);
void vm_frame_access (void *);
#endif // VM_FRAME_H
