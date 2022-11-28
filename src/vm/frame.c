#include "vm/frame.h"

static struct lock frame_lock;
/* The hash map for the node*/
static struct hash frame_hash;
/* The list for LRU*/
static struct list frame_list;

static hash_hash_func frame_hash_function;
static hash_less_func frame_less_function;

struct vm_frame_node
{
  /* physical(kernel) address of this frame */
  void *paddr;

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
}
void
vm_frame_allocate (enum palloc_flags flags)
{
  lock_acquire (&frame_lock);
  void *frame_page = palloc_get_page (PAL_USER | flags);
  // If no empty frame
  if (frame_page == NULL)
    {
      // Try to get a page to evict
      struct vm_frame_node *frame_evict = frame_get_victim ();
    }
}
void
vm_frame_free (void *addr)
{
}
void
vm_frame_access (void *addr)
{
}

static unsigned int
frame_hash_function (const struct hash_elem *e, void *aux)
{
  struct vm_frame_node *n = hash_entry (e, struct vm_frame_node, hash_elem);
  return hash_bytes (&n->paddr, sizeof (n->paddr));
}

static bool
frame_less_function (const struct hash_elem *a, const struct hash_elem *b,
                     void *aux)
{
  struct vm_frame_node *node_a
      = hash_entry (a, struct vm_frame_node, hash_elem);
  struct vm_frame_node *node_b
      = hash_entry (b, struct vm_frame_node, hash_elem);
  return &node_a->paddr < &node_b->paddr;
}