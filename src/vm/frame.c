#include "vm/frame.h"
#include "debug.h"
#include "stdio.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/syscall.h"
#include "vm/swap.h"

#define FRAME_CRITICAL                                                        \
  for (int i = (lock_acquire (&frame_lock), 0); i < 1;                        \
       lock_release (&frame_lock), i++)

static struct lock frame_lock;
/* The hash map for the node*/
static struct hash frame_hash;
/* The list for LRU*/
static struct list frame_list;
static struct list_elem *clock_pointer;

static hash_hash_func page_hash_function;
static hash_less_func page_less_function;

static struct vm_frame *new_frame (void *kpage, void *upage,
                                   struct thread *owner);

/* The function to choose a frame to swap */
static struct vm_frame *frame_get_victim (void);
static void vm_frame_clock_pointer_proceed (void);

/* get the vm_frame according to the kpage */
static struct vm_frame *frame_find_entry (void *kpage);

/* Init the global data*/
void
vm_frame_init ()
{
  lock_init (&frame_lock);
  list_init (&frame_list);
  hash_init (&frame_hash, page_hash_function, page_less_function, NULL);
  clock_pointer = list_tail (&frame_list);
}

/* Allocate frame.
  Do not use this allocate pages for kernel process.
 */
void *
vm_frame_allocate (enum palloc_flags flags, void *page_addr)
{
  // Must contain PAL_USER
  ASSERT (flags & PAL_USER);

  void *frame_page = palloc_get_page (flags);
  struct vm_frame *frame = NULL;
  if (frame_page)
    {
      frame = new_frame (frame_page, page_addr, thread_current ());
      // add the newly allocated frame into present frame list
      FRAME_CRITICAL
      {
        frame->pin = true;
        hash_insert (&frame_hash, &frame->hash_elem);
        list_insert (clock_pointer, &frame->list_elem);
      }
    }
  else
    {
      FRAME_CRITICAL
      {
        // Try to get a page to evict
        struct vm_frame *victim = frame_get_victim ();
        struct thread *owner_old = victim->owner;

        // write down to swap
        swap_idx swap_slot = vm_swap_save (victim->phy_addr);

        // remove the page-frame mapping for the owner
        pagedir_clear_page (owner_old->pagedir, victim->upage_addr);
        struct sup_page_entry *entry = vm_sup_page_find_entry (
            owner_old->supplemental_table, victim->upage_addr);
        ASSERT (entry);
        entry->status = ON_SWAP;
        entry->kpage = NULL;
        entry->swap_slot = swap_slot;

        // reuse the frame
        victim->owner = thread_current ();
        victim->upage_addr = page_addr;
        frame_page = victim->phy_addr;
        memset (frame_page, 0, PGSIZE);

        frame->pin = true;
      }
    }

  return frame_page;
}

/* Release the page.
  If free_resource is set, palloc_free_page.
  (This is a workaround provided for the vm_sup_page_destroy:
  When destroy the pagedir, it will release the resources, but so will the
  sup_page_table. This will release the resources twice. So, we only need to
  remove the entry in our sup_table, and leave the other work for the
  pagedir.

  Since the user doesn't have heap, it will never release its
  resorces when running, we only have to remove them all when the process end.
  Therefore, we don't have to consider the consistency between pagedir and
  sup_page_table.
    )
 */
void
vm_frame_free (void *kpage, bool free_resource)
{
  lock_acquire (&frame_lock);
  ASSERT (is_kernel_vaddr (kpage));
  ASSERT (pg_ofs (kpage) == 0);
  // Find the addr

  struct vm_frame *frame_to_be_freed = frame_find_entry (kpage);
  if (frame_to_be_freed == NULL)
    {
      lock_release (&frame_lock);
      err_exit ();
    }
  // If the pointer points to the one to be freed, move the pointer to next.
  if (clock_pointer == &frame_to_be_freed->list_elem)
    {
      clock_pointer = list_next (clock_pointer);
    }
  list_remove (&frame_to_be_freed->list_elem);
  hash_delete (&frame_hash, &frame_to_be_freed->hash_elem);
  if (free_resource)
    palloc_free_page (frame_to_be_freed->phy_addr);
  free (frame_to_be_freed);

  lock_release (&frame_lock);
}

/* This function implement the Second Chance (or Clock) implementation
 * algorithm. Described here:
 * https://en.wikipedia.org/wiki/Page_replacement_algorithm#Clock
 */
static struct vm_frame *
frame_get_victim ()
{
  // need synchronization
  ASSERT (lock_held_by_current_thread (&frame_lock));

  uint32_t *pd = thread_current ()->pagedir;

  // In case all the frames are accessed. We have to use 2 * n to iterate
  // through the list twice
  for (size_t i = 0; i < 2 * list_size (&frame_list); i++)
    {
      vm_frame_clock_pointer_proceed ();
      struct vm_frame *frame
          = list_entry (clock_pointer, struct vm_frame, list_elem);
      void *upage = frame->upage_addr;

      if (frame->pin)
        continue;

      if (!pagedir_is_accessed (pd, upage))
        {
          if (frame->phy_addr == NULL)
            {
              DEBUG_PRINT ("buggy page: owner:%s, kpage:%p, upage:%p \n",
                           frame->owner->name, frame->phy_addr,
                           frame->upage_addr);
              PANIC ("WTF");
            }
          return frame;
        }
      else
        // Give a second chance
        pagedir_set_accessed (pd, upage, false);
    }
  PANIC ("[VM.FRAME] can evict no frame");
  return NULL;
}

void
vm_frame_pin_upd (void *kpage, bool pin)
{
  ASSERT (kpage);
  FRAME_CRITICAL
  {
    struct vm_frame *frame = frame_find_entry (kpage);
    ASSERT (frame);
    frame->pin = pin;
  }
}

static struct vm_frame *
frame_find_entry (void *kpage)
{
  struct vm_frame t;
  t.phy_addr = kpage;
  struct hash_elem *e = hash_find (&frame_hash, &t.hash_elem);
  return e ? hash_entry (e, struct vm_frame, hash_elem) : NULL;
}

/* Update the clock pointer. The pointer go throught the list circularly.*/
static void
vm_frame_clock_pointer_proceed (void)
{
  ASSERT (!list_empty (&frame_list));
  if (clock_pointer == list_tail (&frame_list)
      || clock_pointer == list_end (&frame_list))
    {
      clock_pointer = list_begin (&frame_list);
    }
  else
    {
      clock_pointer = list_next (clock_pointer);
    }
}

static unsigned int
page_hash_function (const struct hash_elem *e, void *aux UNUSED)
{
  struct vm_frame *n = hash_entry (e, struct vm_frame, hash_elem);
  return hash_int ((int)n->phy_addr);
}

static bool
page_less_function (const struct hash_elem *a, const struct hash_elem *b,
                    void *aux UNUSED)
{
  struct vm_frame *node_a = hash_entry (a, struct vm_frame, hash_elem);
  struct vm_frame *node_b = hash_entry (b, struct vm_frame, hash_elem);
  return node_a->phy_addr < node_b->phy_addr;
}

static struct vm_frame *
new_frame (void *kpage, void *upage, struct thread *owner)
{
  struct vm_frame *f = (struct vm_frame *)calloc (sizeof (struct vm_frame), 1);
  if (f)
    {
      f->owner = owner;
      f->phy_addr = kpage;
      f->upage_addr = upage;
    }
  return f;
}