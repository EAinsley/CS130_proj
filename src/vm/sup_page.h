#ifndef SUP_PAGE_H
#define SUP_PAGE_H
#include "lib/kernel/hash.h"
#include "lib/string.h"
#include "threads/malloc.h"
#include "userprog/pagedir.h"
#include "vm/frame.h"

enum SUP_PAGE_STATUS
{
  LOADED,
  ZERO,
  IN_FILE,
  ON_SWAP
};

struct sup_page_table
{
  struct hash hash_table;
};

struct sup_page_entry
{

  enum SUP_PAGE_STATUS status;
  /* User space page addr*/
  void *upage;
  /* Kernal space addr*/
  void *kpage;

  /* hash table node used to map the upage onto the table.*/
  struct hash_elem hash_elem;
};

struct sup_page_table *vm_sup_page_create (void);
void vm_sup_page_destroy (struct sup_page_table *);

// The installs are lazy load. When try to access the real data, you must load
// the frame first.
bool vm_sup_page_install_frame (struct sup_page_table *table, uint32_t *pd,
                                void *upage, void *kpage);
bool vm_sup_page_install_zero_frame (struct sup_page_table *table,
                                     uint32_t *pd, void *upage);
bool vm_sup_page_install_files (struct sup_page_table *table, uint32_t *pd,
                                void *upage);
bool vm_sup_page_remove_frame (struct sup_page_table *table, uint32_t *pd,
                               void *upage);
bool vm_sup_page_load_frame (struct sup_page_table *table, uint32_t *pd,
                             void *upage);
#endif // SUP_PAGE_H