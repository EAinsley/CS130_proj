#include "vm/sup_page.h"

/* helper functions */
static hash_hash_func page_hash_function;
static hash_less_func page_less_function;
static hash_action_func page_destroy_function;
/* Create a supplemental page table. This function should be called during the
 * process initialization.*/
struct vm_sup_page_table *
vm_sup_page_create (void)
{
  struct vm_sup_page_table *table
      = (struct vm_sup_page_table *)malloc (sizeof (struct vm_sup_page_table));
  hash_init (&table->hash_table, page_hash_function, page_less_function, NULL);
  return table;
}
/* Destroy the supplemental page table and release all the resources allocated
 * on it.*/
void
vm_sup_page_destroy (struct vm_sup_page_table *page_table)
{
  hash_destroy (&page_table->hash_table, page_destroy_function);
}

bool
vm_sup_page_install_page (struct vm_sup_page_table *table, void *upage,
                          void *kpage)
{
  struct sup_page_entry *entry
      = (struct sup_page_entry *)malloc (sizeof (struct sup_page_entry));
  // allocation failed
  if (entry == NULL)
    return false;

  entry->status = LOADED;
  entry->upage = upage;
  entry->kpage = kpage;
  entry->swap_slot = 0;
  if (hash_insert (&table->hash_table, &entry->hash_elem) != NULL)
    {
      // Insertion failed, upage already exist
      free (entry);
      return false;
    }

  return true;
}

bool
vm_sup_page_install_zero_page (struct vm_sup_page_table *table, void *upage)
{
  struct sup_page_entry *entry
      = (struct sup_page_entry *)malloc (sizeof (struct sup_page_entry));
  // allocation failed
  if (entry == NULL)
    return false;
  entry->status = ZERO;
  entry->upage = upage;
  entry->kpage = NULL; // Not set yet

  if (hash_insert (&table->hash_table, &entry->hash_elem) != NULL)
    {
      // upage already exist
      return false;
    }

  return true;
}

bool
vm_sup_page_install_files (struct vm_sup_page_table *table UNUSED,
                           void *upage UNUSED)
{
  /* TODO - Not implemented yet */
  PANIC ("unimplemted: install page from file");
}

bool
vm_sup_page_load_page (struct vm_sup_page_table *table, uint32_t *pd,
                       void *upage)
{
  struct sup_page_entry *entry = vm_sup_page_find_entry (table, upage);
  // If the entry doesn't exist, there must be something wrong.
  if (entry == NULL)
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
      // ELF lazy load
      // TODO - Not implemented yet.
      PANIC ("ELF lazy load unimplemented");
      break;
    case ON_SWAP:
      vm_swap_load (kpage, entry->swap_slot);
      break;
    default:
      PANIC ("unreachable code");
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
struct sup_page_entry *
vm_sup_page_find_entry (struct vm_sup_page_table *table, void *upage)
{
  struct sup_page_entry t;
  t.upage = upage;
  struct hash_elem *e = hash_find (&table->hash_table, &t.hash_elem);
  return hash_entry (e, struct sup_page_entry, hash_elem);
}

/* hash functions */
static unsigned int
page_hash_function (const struct hash_elem *e, void *aux UNUSED)
{
  struct sup_page_entry *n = hash_entry (e, struct sup_page_entry, hash_elem);
  return hash_int (pg_no (n->upage));
}

static bool
page_less_function (const struct hash_elem *a, const struct hash_elem *b,
                    void *aux UNUSED)
{
  struct sup_page_entry *node_a
      = hash_entry (a, struct sup_page_entry, hash_elem);
  struct sup_page_entry *node_b
      = hash_entry (b, struct sup_page_entry, hash_elem);
  return node_a->upage < node_b->upage;
}

static void
page_destroy_function (struct hash_elem *e, void *aux UNUSED)
{
  struct sup_page_entry *n = hash_entry (e, struct sup_page_entry, hash_elem);
  // Free the frame
  if (n->kpage != NULL)
    vm_frame_free (n->kpage, false);
  // discard the pages in SWAP
  if (n->status == ON_SWAP)
    vm_swap_discard (n->swap_slot);
  // Frae the entry
  free (n);
}