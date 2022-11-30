#include "vm/frame.h"
static struct lock frame_lock;
/* The hash map for the node*/
static struct hash frame_hash;
/* The list for LRU*/
static struct list frame_list;
static struct list_elem *clock_pointer;

static hash_hash_func page_hash_function;
static hash_less_func page_less_function;

/* The function to choose a frame to swap */
static struct vm_frame_node *frame_get_victim (void);
static void vm_frame_clock_pointer_proceed (void);

/* get the vm_frame_node according to the kpage */
static struct vm_frame_node *frame_find_entry (void *kpage);

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
  lock_acquire (&frame_lock);
  // Must contain PAL_USER
  void *frame_page = palloc_get_page (PAL_USER | flags);
  // If no empty frame
  if (frame_page == NULL)
    {
      // Try to get a page to evict
      struct vm_frame_node *frame_evict UNUSED = frame_get_victim ();
      /* TODO - swap the page */
      /* NOTE - swap haven't been implemented yet, panic the kernel */
      ASSERT (false);
    }
  // Allocate memory for the frame.
  struct vm_frame_node *frame
      = (struct vm_frame_node *)malloc (sizeof (struct vm_frame_node));
  // Allocation failed
  if (frame == NULL)
    {
      // FIXME - This release should not be here.
      lock_release (&frame_lock);
      return NULL;
    }
  // Initialize the frame
  frame->phy_addr = frame_page;
  frame->upage_addr = page_addr;
  hash_insert (&frame_hash, &frame->hash_elem);
  list_insert (clock_pointer, &frame->list_elem);

  lock_release (&frame_lock);
  return frame_page;
}

/* Release the page */
void
vm_frame_free (void *kpage)
{
  lock_acquire (&frame_lock);
  ASSERT (is_kernel_vaddr (kpage));
  ASSERT (pg_ofs (kpage) == 0);
  // Find the addr

  struct vm_frame_node *frame_to_be_freed = frame_find_entry (kpage);
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
  // palloc_free_page (frame_to_be_freed->phy_addr);
  free (frame_to_be_freed);

  lock_release (&frame_lock);
}

/* This function implement the Second Chance (or Clock) implementation
 * algorithm. Described here:
 * https://en.wikipedia.org/wiki/Page_replacement_algorithm#Clock
 */
static struct vm_frame_node *
frame_get_victim ()
{
  // In case all the frames are accessed. We have to use 2 * n to iterate
  // through the list twice
  // NOTE - (Or maybe n+1?).
  for (size_t i = 0; i < 2 * list_size (&frame_list); i++)
    {
      vm_frame_clock_pointer_proceed ();
      struct vm_frame_node *frame
          = list_entry (clock_pointer, struct vm_frame_node, list_elem);
      // Give a seconde chance
      if (pagedir_is_accessed (thread_current ()->pagedir, frame->upage_addr))
        {
          pagedir_set_accessed (thread_current ()->pagedir, frame->upage_addr,
                                false);
        }
      else
        {
          return frame;
        }
    }
  return NULL;
}

static struct vm_frame_node *
frame_find_entry (void *kpage)
{
  struct vm_frame_node t;
  t.phy_addr = kpage;
  struct hash_elem *e = hash_find (&frame_hash, &t.hash_elem);
  return hash_entry (e, struct vm_frame_node, hash_elem);
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
  struct vm_frame_node *n = hash_entry (e, struct vm_frame_node, hash_elem);
  return hash_int ((int)n->phy_addr);
}

static bool
page_less_function (const struct hash_elem *a, const struct hash_elem *b,
                    void *aux UNUSED)
{
  struct vm_frame_node *node_a
      = hash_entry (a, struct vm_frame_node, hash_elem);
  struct vm_frame_node *node_b
      = hash_entry (b, struct vm_frame_node, hash_elem);
  return node_a->phy_addr < node_b->phy_addr;
}