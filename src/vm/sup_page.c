#include "vm/sup_page.h"

/* hash map used to hold the frame mapping*/
struct hash sup_page_hash;

/* hash function operation*/
static struct sup_page_entry *page_find_entry (void *upage);
/* helper functions */
static hash_hash_func page_hash_function;
static hash_less_func page_less_function;
static hash_action_func frame_destroy_function;
/* Create a supplemental page table. This function should be called during the
 * process initialization.*/
struct sup_page_table *
vm_sup_page_create (void)
{
  struct sup_page_table *table = malloc (sizeof (struct sup_page_table));
  hash_init (&table->hash_table, page_hash_function, page_less_function, NULL);
  return table;
}
/* Destroy the supplemental page table and release all the resources allocated
 * on it.*/
void
vm_sup_page_destroy (struct sup_page_table *page_table)
{
  hash_destroy (&page_table->hash_table, frame_destroy_function);
}

bool
vm_sup_page_install_frame (struct sup_page_table *table, uint32_t *pd,
                           void *upage, void *kpage)
{
  /* TODO - Not implemented yet */
  ASSERT (false);
}

bool
vm_sup_page_install_zero_frame (struct sup_page_table *table, uint32_t *pd,
                                void *upage)
{
  struct sup_page_entry *entry = malloc (sizeof (struct sup_page_entry));
  entry->status = ZERO;
  entry->upage = upage;
  entry->kpage = NULL; // Not set yet

  if (hash_insert (&sup_page_hash, &entry->hash_elem) == NULL)
    {
      // upage already exist
      return false;
    }

  return true;
}

bool
vm_sup_page_install_files (struct sup_page_table *table, uint32_t *pd,
                           void *upage)
{
  /* TODO - Not implemented yet */
  ASSERT (false);
}

bool
vm_sup_page_remove_frame (struct sup_page_table *table, uint32_t *pd,
                          void *upage)
{
  /* TODO - Not implemented yet */
  ASSERT (false);
}

bool
vm_sup_page_load_frame (struct sup_page_table *table, uint32_t *pd,
                        void *upage)
{
  struct sup_page_entry *entry = page_find_entry (upage);
  // If the entry doesn't exist, there must be something wrong. Report the
  // result.
  if (entry != NULL)
    {
      return false;
    }
  // Already loaded. Make this an idempotent operation.
  if (entry->status == LOADED)
    {
      return true;
    }
  void *kpage = vm_frame_allocate (PAL_USER, upage);
  // Allocation failed
  if (kpage == NULL)
    {
      return false;
    }

  switch (entry->status)
    {
    case ZERO:
      memset (kpage, 0, PGSIZE);
      break;
    case IN_FILE:
      // TODO - Not implemented yet.
      ASSERT (false)
      break;
    case ON_SWAP:
      // TODO - Not implemented yet.
      ASSERT (false);
      break;
    default:
      break;
    }

  // Set page directory
  if (!pagedir_set_page (pd, upage, kpage, true))
    {
      // Memory allocation failed.
      return false;
    }
  // Clean page
  pagedir_set_dirty (pd, upage, false);

  // Finale
  entry->kpage = kpage;
  entry->status = LOADED;
  return true;
}

/* Find the hash element with the given upage. Returns NULL if it doesn't
 * exist. */
static struct sup_page_entry *
page_find_entry (void *upage)
{
  struct sup_page_entry t;
  t.upage = upage;
  struct hash_page_entry *entry = hash_find (&sup_page_hash, &t.hash_elem);
  return entry;
}

/* hash functions */
static unsigned int
page_hash_function (const struct hash_elem *e, void *aux)
{
  struct sup_page_entry *n = hash_entry (e, struct sup_page_entry, hash_elem);
  return hash_bytes (&n->upage, sizeof (n->upage));
}

static bool
page_less_function (const struct hash_elem *a, const struct hash_elem *b,
                    void *aux)
{
  struct sup_page_entry *node_a
      = hash_entry (a, struct sup_page_entry, hash_elem);
  struct sup_page_entry *node_b
      = hash_entry (b, struct sup_page_entry, hash_elem);
  return &node_a->upage < &node_b->upage;
}

static void
frame_destroy_function (struct hash_elem *e, void *aux)
{
  struct sup_page_entry *n = hash_entry (e, struct sup_page_entry, hash_elem);
  // Free the frame
  vm_frame_free (n->kpage);
  // Frae the entry
  free (n);
}