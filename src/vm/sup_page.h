#ifndef SUP_PAGE_H
#define SUP_PAGE_H
#include "lib/kernel/hash.h"
#include "lib/string.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "vm/frame.h"
#include "vm/swap.h"

enum SUP_PAGE_STATUS
{
  LOADED,
  ZERO,
  IN_FILE,
  ON_SWAP
};

struct vm_sup_page_table
{
  struct hash hash_table;
};

struct sup_page_entry
{
  /* status of this page table entry */
  enum SUP_PAGE_STATUS status;
  /* User space page addr*/
  void *upage;
  /* Kernal space addr*/
  void *kpage;

  /*
  The slot index in SWAP where this page is stored.
  Only applicable when status==ON_SWAP
  */
  swap_idx swap_slot;

  /* hash table node used to map the upage onto the table.*/
  struct hash_elem hash_elem;
};

struct vm_sup_page_table *vm_sup_page_create (void);
void vm_sup_page_destroy (struct vm_sup_page_table *);

// The installs are lazy load. When try to access the real data, you must load
// the frame first.
bool vm_sup_page_install_page (struct vm_sup_page_table *table, void *upage,
                               void *kpage);
bool vm_sup_page_install_zero_page (struct vm_sup_page_table *table,
                                    void *upage);
bool vm_sup_page_install_files (struct vm_sup_page_table *table, void *upage);
bool vm_sup_page_load_page (struct vm_sup_page_table *table, uint32_t *pd,
                            void *upage);

/* hash function operation*/
struct sup_page_entry *vm_sup_page_find_entry (struct vm_sup_page_table *table,
                                               void *upage);
#endif // SUP_PAGE_H