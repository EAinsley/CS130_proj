#ifndef SUP_PAGE_H
#define SUP_PAGE_H
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "filesys/off_t.h"
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

struct lazy_load_page
{
  /* if mapped==true:
     - evict/destory AND dirty  => write back
     - unmap AND last_map_page => close file
   */
  struct file *f;
  off_t ofs;
  uint32_t len;
  bool w;
};

struct sup_page_entry
{
  /* status of this page table entry */
  enum SUP_PAGE_STATUS status;
  /* User space page addr*/
  void *upage;
  /* Kernal space addr*/
  void *kpage;

  /* permission */
  bool writable;

  /* whether this page is a memory-file mapping */
  bool mapped;

  /*
  a lazy load page from the ELF executable file
  Only applicable when `status==IN_FILE ||`
  */
  struct lazy_load_page lazy_load;

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
bool vm_sup_page_install_files (struct vm_sup_page_table *table, void *upage,
                                struct file *f, off_t ofs,
                                uint32_t page_read_bytes, bool writable);
bool vm_sup_page_load_page (struct vm_sup_page_table *table, uint32_t *pd,
                            void *upage);

/* hash function operation*/
struct sup_page_entry *vm_sup_page_find_entry (struct vm_sup_page_table *table,
                                               void *upage);

/*
  add a mmap page in the page-table.

  - fill the last `PGSIZE-bytes` bytes in the page to zero when loading.
  - only write back `bytes` bytes in the beginning of this page
 */
bool vm_sup_page_map (struct vm_sup_page_table *table, void *upage,
                      struct file *f, off_t ofs, uint32_t bytes);
/*
  remove a mmap section in the page-table
  `upage_begin` is the first mapped page,
  also close the mapping file
*/
void vm_sup_page_unmap (struct vm_sup_page_table *table, void *upage_begin,
                        uint32_t pages);

/* writeback a mmap page */
void vm_sup_page_writeback (struct sup_page_entry *entry);

#endif // SUP_PAGE_H