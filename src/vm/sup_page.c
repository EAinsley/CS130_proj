#include "vm/sup_page.h"
#include "stdio.h"

static struct sup_page_entry *vm_ste_new (void);

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
  struct sup_page_entry *entry = vm_ste_new ();
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
  struct sup_page_entry *entry = vm_ste_new ();
  if (entry == NULL)
    return false;

  entry->status = ZERO;
  entry->upage = upage;
  entry->kpage = NULL; // Not set yet

  if (hash_insert (&table->hash_table, &entry->hash_elem) != NULL)
    {
      // insert failed: release the entry, prevent leaking
      free (entry);
      return false;
    }

  return true;
}

// (table, upage, file, ofs, page_read_bytes, writable);

bool
vm_sup_page_install_files (struct vm_sup_page_table *table, void *upage,
                           struct file *f, off_t ofs, uint32_t page_read_bytes,
                           bool w)
{
  struct sup_page_entry *entry = vm_ste_new ();
  if (entry == NULL)
    return false;
  entry->status = IN_FILE;
  entry->upage = upage;
  entry->kpage = NULL;

  // wait for lazy load
  entry->lazy_load.f = f;
  entry->lazy_load.ofs = ofs;
  entry->lazy_load.len = page_read_bytes;
  entry->lazy_load.w = w;

  if (hash_insert (&table->hash_table, &entry->hash_elem) != NULL)
    {
      // insert failed: release the entry, prevent leaking
      free (entry);
      return false;
    }
  return true;
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
  if (kpage == NULL)
    {
      // Allocation failed, the page cannot be loaded
      return false;
    }

  bool writable = true;
  switch (entry->status)
    {
    case ZERO:
      memset (kpage, 0, PGSIZE);
      break;
    case IN_FILE:
      {
        off_t ofs = entry->lazy_load.ofs;
        uint32_t len = entry->lazy_load.len;
        struct file *f = entry->lazy_load.f;
        writable = entry->lazy_load.w;
        printf ("load page %p, with permission %d\n", upage, (int)writable);

        file_seek (f, ofs);
        if ((off_t)len != file_read (f, kpage, len))
          {
            // something wrong in file system read...
            return false;
          }
        // lazy load PAGE: bytes_to_read + zero_bytes
        memset (kpage + len, 0, PGSIZE - len);
      }
      break;
    case ON_SWAP:
      vm_swap_load (kpage, entry->swap_slot);
      break;
    default:
      PANIC ("unreachable code");
      break;
    }

  // Set page directory
  if (!pagedir_set_page (pd, upage, kpage, writable))
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

static struct sup_page_entry *
vm_ste_new ()
{
  struct sup_page_entry *entry
      = (struct sup_page_entry *)calloc (sizeof (struct sup_page_entry), 1);
  return entry;
}