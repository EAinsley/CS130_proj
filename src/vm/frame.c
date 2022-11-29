#include "vm/frame.h"
static struct lock frame_lock;
/* The hash map for the node*/
static struct hash frame_hash;
/* The list for LRU*/
static struct list frame_list;
static struct list_elem *clock_pointer;

static hash_hash_func frame_hash_function;
static hash_less_func frame_less_function;

/* The function to choose a frame to swap */
static struct vm_frame_node *frame_get_victim (void);
static struct vm_frame_node *vm_frame_clock_pointer_next (void);
static void vm_frame_update_clock_pointer (void);
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

/* Init the global data*/
void
vm_frame_init ()
{
  lock_init (&frame_lock);
  list_init (&frame_list);
  hash_init (&frame_hash, frame_hash_function, frame_less_function, NULL);
  clock_pointer = list_tail (&frame_list);
}
void *
vm_frame_allocate (enum palloc_flags flags, void *page_addr)
{
  lock_acquire (&frame_lock);
  void *frame_page = palloc_get_page (PAL_USER | flags);
  // If no empty frame
  if (frame_page == NULL)
    {
      // Try to get a page to evict
      struct vm_frame_node *frame_evict = frame_get_victim ();
      /* TODO - swap the page */
      /* NOTE - swap haven't been implemented yet, panic the kernel */
      ASSERT (false);
    }
  // Allocate memory for the frame.
  struct vm_frame_node *frame = malloc (sizeof (struct vm_frame_node));
  // Allocation failed
  if (!frame)
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

// Release the page
void
vm_frame_free (void *addr)
{
  lock_acquire (&frame_lock);
  ASSERT (is_kernel_vaddr (addr));
  ASSERT (pg_ofs (addr) == 0);
  // Find the addr
  struct vm_frame_node t;
  t.phy_addr = addr;
  struct vm_frame_node *frame_to_be_freed
      = hash_find (&frame_hash, &t.hash_elem);
  if (frame_to_be_freed == NULL)
    {
      lock_release (&frame_lock);
      err_exit ();
    }
  list_remove (&frame_to_be_freed->list_elem);
  hash_delete (&frame_hash, &frame_to_be_freed->hash_elem);
  free (frame_to_be_freed->phy_addr);

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
  for (int i = 0; i < 2 * list_size (&frame_list); i++)
    {
      vm_frame_update_clock_pointer ();
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
}

/* Update the clock pointer. The pointer go throught the list circularly.*/
static void
vm_frame_update_clock_pointer (void)
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
frame_hash_function (const struct hash_elem *e, void *aux)
{
  struct vm_frame_node *n = hash_entry (e, struct vm_frame_node, hash_elem);
  return hash_bytes (&n->phy_addr, sizeof (n->phy_addr));
}

static bool
frame_less_function (const struct hash_elem *a, const struct hash_elem *b,
                     void *aux)
{
  struct vm_frame_node *node_a
      = hash_entry (a, struct vm_frame_node, hash_elem);
  struct vm_frame_node *node_b
      = hash_entry (b, struct vm_frame_node, hash_elem);
  return &node_a->phy_addr < &node_b->phy_addr;
}