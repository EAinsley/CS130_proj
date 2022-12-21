#include "userprog/process.h"
#include "devices/timer.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "lib/string.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);

static void prepare_stack (void **esp, char *name, char *args);
static void esp_push_align (void **esp_);
static void esp_push_u32 (void **esp_, uint32_t val);
static void esp_push_ptr (void **esp_, void *ptr);

/* arguments for the start_process thread function*/
struct child_arg
{
  struct thread *parent;       // pointer to parent process PCB
  struct semaphore sema_start; // up child process started
  char *fn_copy;               // copy of the file name
  bool load_failed;            // whether load ELF has succeeded
};

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *file_name)
{
  struct thread *cur = thread_current ();
  struct child_arg ch_arg;
  memset (&ch_arg, 0, sizeof (ch_arg));
  ch_arg.parent = cur;
  sema_init (&ch_arg.sema_start, 0);

  // a kernel thread is trying to spawn a user program process
  // create the process record data structure for this thread
  // remember to deallocate on this kernel thread exit
  if (cur->proc == NULL)
    {
      cur->proc = (struct proc_record *)palloc_get_page (0);
      ASSERT (cur->proc);
      proc_init (cur->proc);
      cur->proc->orphan = true;
    }

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  ch_arg.fn_copy = palloc_get_page (0);
  if (ch_arg.fn_copy == NULL)
    return TID_ERROR;
  strlcpy (ch_arg.fn_copy, file_name, PGSIZE);

  /* Create a new thread to execute FILE_NAME. */
  tid_t tid = thread_create (file_name, PRI_DEFAULT, start_process, &ch_arg);
  // on thread create error
  if (tid == TID_ERROR)
    return -1;
  // make sure that parent process won't exit before child creation finished
  sema_down (&ch_arg.sema_start);
  // deallocate the file name copy
  palloc_free_page (ch_arg.fn_copy);
  // start process function failed to load ELF executable
  if (ch_arg.load_failed)
    return -1;
  return tid;
}

/* A thread function that loads a user process and starts it
   running.
   1. parse arguments from `args`,
   2. push them onto the stack that `esp` is pointing to,
   3. follow X86 call convention
*/
static void
start_process (void *ch_arg_)
{
  struct child_arg *ch_arg = (struct child_arg *)ch_arg_;
  char *file_name = ch_arg->fn_copy, *args = NULL;
  struct intr_frame if_;
  bool success;
  struct thread *cur = thread_current ();

  // /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;

  // find the path to ELF binary file
  strtok_r (file_name, " ", &args);
  // set correct thread name
  strlcpy (cur->name, file_name, strlen (file_name) + 1);

  // create the process control data structures
  cur->proc = (struct proc_record *)palloc_get_page (0);
  ASSERT (cur->proc);
  proc_init (cur->proc);
  // load the ELF binary executable file from FS
  success = load (file_name, &if_.eip, &if_.esp);
  if (success)
    prepare_stack (&if_.esp, file_name, args);
  else
    ch_arg->load_failed = true;

  // insert child process record pointer for parent
  proc_add_child (ch_arg->parent->proc, cur->proc);
  // notify the parent that the child process is ready
  sema_up (&ch_arg->sema_start);

  /* If load failed, quit. */
  if (!success)
    {
      // load error, set error exit flag and status code
      cur->proc->proc_status = PROC_ERROR_EXIT;
      cur->proc->exit_code = -1;
      thread_exit ();
    }

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile("movl %0, %%esp; jmp intr_exit" : : "g"(&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int
process_wait (tid_t tid)
{
  struct proc_record *child_proc = proc_find_child (tid);
  // not a children or already removed from children list
  if (child_proc == NULL)
    return -1;
  // wait for child process exit
  sema_down (&child_proc->sema_exit);
  int code = child_proc->exit_code, reason = child_proc->proc_status;
  // deallocate resources in proc_record for the child
  proc_remove_child (child_proc);
  palloc_free_page ((void *)child_proc);
  // the child process was killed by kernel on exception, return -1
  if (reason == PROC_ERROR_EXIT)
    return -1;
  return code;
}

/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();

  // not a userprog process
  if (cur->pagedir == NULL)
    {
      // deallocate the process record if previously allocated
      if (cur->proc != NULL)
        palloc_free_page (cur->proc);
      cur->proc = NULL;
      return;
    }

  // print the process exit message
  printf ("%s: exit(%d)\n", cur->name, cur->proc->exit_code);

  // possible race: child exit + parent exit interleaved
  enum intr_level old = intr_disable ();
  // deallocate the proc record for already exited children,
  // set running children to be orphan.
  for (int i = 0; i < MAX_CHS; i++)
    {
      struct proc_record *ch_proc = cur->proc->children[i];
      if (ch_proc != NULL)
        {
          if (ch_proc->proc_status == PROC_RUNNING)
            ch_proc->orphan = true;
          else
            palloc_free_page ((void *)ch_proc);
        }
    }
  intr_set_level (old);

  /* Destroy the current process's page directory and switch back to the
     kernel-only page directory.

     Correct ordering here is crucial.  We must set cur->pagedir to NULL before
     switching page directories, so that a timer interrupt can't switch back to
     the process page directory.  We must activate the base page directory
     before destroying the process's page directory, or our active page
     directory will be one that's been freed (and cleared).
     */
  uint32_t *pd = cur->pagedir;
  cur->pagedir = NULL;
  pagedir_activate (NULL);
  pagedir_destroy (pd);

  // Allow write and close file.
  if (cur->proc->image != NULL)
    {
      file_allow_write (cur->proc->image);
      file_close (cur->proc->image);
    }

  // NOTE - Memory freed here!
  fd_list_clear (&cur->proc->fd_list);
  // notify parent thread that the children have exitted
  sema_up (&cur->proc->sema_exit);

  // orphran deallocate process record structure
  if (cur->proc->orphan)
    palloc_free_page ((void *)cur->proc);
  cur->proc = NULL;
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32 /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32 /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32 /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16 /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
{
  unsigned char e_ident[16];
  Elf32_Half e_type;
  Elf32_Half e_machine;
  Elf32_Word e_version;
  Elf32_Addr e_entry;
  Elf32_Off e_phoff;
  Elf32_Off e_shoff;
  Elf32_Word e_flags;
  Elf32_Half e_ehsize;
  Elf32_Half e_phentsize;
  Elf32_Half e_phnum;
  Elf32_Half e_shentsize;
  Elf32_Half e_shnum;
  Elf32_Half e_shstrndx;
};

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
{
  Elf32_Word p_type;
  Elf32_Off p_offset;
  Elf32_Addr p_vaddr;
  Elf32_Addr p_paddr;
  Elf32_Word p_filesz;
  Elf32_Word p_memsz;
  Elf32_Word p_flags;
  Elf32_Word p_align;
};

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL 0           /* Ignore. */
#define PT_LOAD 1           /* Loadable segment. */
#define PT_DYNAMIC 2        /* Dynamic linking info. */
#define PT_INTERP 3         /* Name of dynamic loader. */
#define PT_NOTE 4           /* Auxiliary info. */
#define PT_SHLIB 5          /* Reserved. */
#define PT_PHDR 6           /* Program header table. */
#define PT_STACK 0x6474e551 /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1 /* Executable. */
#define PF_W 2 /* Writable. */
#define PF_R 4 /* Readable. */

static bool setup_stack (void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *file_name, void (**eip) (void), void **esp)
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL)
    goto done;
  process_activate ();

  /* Open executable file. */
  file = filesys_open (file_name);
  if (file == NULL)
    {
      printf ("load: %s: open failed\n", file_name);
      goto done;
    }
  file_deny_write (file);

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7) || ehdr.e_type != 2
      || ehdr.e_machine != 3 || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr) || ehdr.e_phnum > 1024)
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done;
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++)
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type)
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file))
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *)mem_page, read_bytes,
                                 zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp))
    goto done;

  /* Start address. */
  *eip = (void (*) (void))ehdr.e_entry;

  success = true;

done:
  /* We arrive here whether the load is successful or not. */
  if (success)
    {
      proc_current ()->image = file;
    }
  else
    {
      if (file != NULL)
        file_allow_write (file);
      file_close (file);
    }
  return success;
}

/* load() helpers. */

static bool install_page (void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file)
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
    return false;

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off)file_length (file))
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz)
    return false;

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;

  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *)phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *)(phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0)
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      /* Get a page of memory. */
      uint8_t *kpage = palloc_get_page (PAL_USER);
      if (kpage == NULL)
        return false;

      /* Load this page. */
      if (file_read (file, kpage, page_read_bytes) != (int)page_read_bytes)
        {
          palloc_free_page (kpage);
          return false;
        }
      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      /* Add the page to the process's address space. */
      if (!install_page (upage, kpage, writable))
        {
          palloc_free_page (kpage);
          return false;
        }

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp)
{
  uint8_t *kpage;
  bool success = false;

  kpage = palloc_get_page (PAL_USER | PAL_ZERO);
  if (kpage != NULL)
    {
      success = install_page (((uint8_t *)PHYS_BASE) - PGSIZE, kpage, true);
      if (success)
        *esp = PHYS_BASE;
      else
        palloc_free_page (kpage);
    }
  return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}

/* SECTION-BEGIN */
/* SECTION-BEGIN: prepare stack */
/* SECTION-BEGIN */

/* push a string onto stack */
static void
esp_push_str (void **esp_, char *str)
{
  // [p, p+L-1]  [p+L]  [p+L+1 = esp]
  // cmd string  NULL   last sp
  char *esp = (char *)*esp_;
  int len = strlen (str);
  esp = esp - len - 1;
  strlcpy (esp, str, len + 1);
  *esp_ = esp;
}
/* push word align onto stack */
static void
esp_push_align (void **esp_)
{
  char *esp = (char *)*esp_;
  while ((uint32_t)esp % 4 != 0)
    {
      *--esp = 0;
    }
  *esp_ = esp;
}

/* push a uint32 onto stack */
static void
esp_push_u32 (void **esp_, uint32_t val)
{
  uint32_t *esp = (uint32_t *)*esp_;
  *--esp = val;
  *esp_ = esp;
}

/* push a pointer onto stack */
static void
esp_push_ptr (void **esp_, void *ptr)
{
  esp_push_u32 (esp_, (uint32_t)ptr);
}

/* split arguments from command line,
   populate stakc, follow X86 call convention.
*/
static void
prepare_stack (void **esp, char *name, char *args)
{
  ASSERT (*esp != NULL);

  uint32_t argc = 0;
  char *argv[64];

  // copy the command line onto stack
  esp_push_str (esp, args);
  // point to the start of command line string
  args = (char *)*esp;

  esp_push_str (esp, name);
  argv[argc++] = (char *)*esp;
  // word align
  esp_push_align (esp);

  // split and parse arguments
  argv[argc++] = strtok_r (args, " ", &args);
  while (argv[argc - 1] != NULL)
    {
      argv[argc++] = strtok_r (NULL, " ", &args);
    }
  argc--;

  // push pointers to each argument (argv array)
  for (int i = argc; i >= 0; i--)
    {
      esp_push_ptr (esp, argv[i]);
    }
  // push pointer to the (argv array) onto stack
  esp_push_ptr (esp, *esp);
  // push argc onto stack
  esp_push_u32 (esp, argc);
  // push return address
  esp_push_ptr (esp, NULL);
}

/* SECTION-END */
/* SECTION-END: prepare stack */
/* SECTION-END */

/* SECTION-BEGIN */
/* SECTION-BEGIN: children process management */
/* SECTION-BEGIN */

/* initialize a process record */
void
proc_init (struct proc_record *proc)
{
  ASSERT (proc != NULL);
  memset ((void *)proc, 0, sizeof (struct proc_record));
  proc->id = thread_tid ();
  sema_init (&proc->sema_exit, 0);
  list_init (&proc->fd_list);
  proc->proc_status = PROC_RUNNING;
  proc->image = NULL;
}

/* find the process whose thread id equals to the given id and return the
 * process record */
struct proc_record *
proc_find (tid_t id)
{
  struct proc_record *proc = NULL;

  // possible race: searching the list, updating the list
  // disable interrupt to have exclusive access to the thread list
  enum intr_level old = intr_disable ();
  struct thread *th = thread_find (id);
  if (th)
    proc = th->proc;
  intr_set_level (old);

  return proc;
}

/* Find a child process with the given id.
   Return the process structure pointer if found.
   Otherwise, return NULL
 */
struct proc_record *
proc_find_child (tid_t id)
{
  if (thread_current ()->proc == NULL)
    return NULL;
  struct proc_record **chs = thread_current ()->proc->children;
  for (int i = 0; i < MAX_CHS; i++)
    if (chs[i] && chs[i]->id == id)
      return chs[i];
  return NULL;
}
/* add a child process into the children filed of parent */
void
proc_add_child (struct proc_record *proc, struct proc_record *child)
{
  if (proc == NULL)
    return;

  child->orphan = false;
  struct proc_record **chs = proc->children;
  for (int i = 0; i < MAX_CHS; i++)
    if (chs[i] == NULL)
      {
        chs[i] = child;
        return;
      }
  NOT_REACHED ();
}
/* add a new child process id to the children list of current thread */
void
proc_remove_child (struct proc_record *child_proc)
{
  struct proc_record **chs = thread_current ()->proc->children;
  for (int i = 0; i < MAX_CHS; i++)
    if (chs[i] == child_proc)
      {
        chs[i] = NULL;
        return;
      }
  NOT_REACHED ();
}

struct proc_record *
proc_current ()
{
  return thread_current ()->proc;
}

/* SECTION-END */
/* SECTION-END: children process management */
/* SECTION-END */

/* clear the fd_list*/
void
fd_list_clear (struct list *fl)
{
  enum intr_level old_level = intr_disable ();
  while (!list_empty (fl))
    {
      struct list_elem *e = list_pop_front (fl);
      struct fd_node *fd_node = list_entry (e, struct fd_node, fd_elem);
      file_close (fd_node->f);
      free (fd_node);
    }
  intr_set_level (old_level);
}

/* insert a new file, allocating fd */
int
fd_list_insert (struct list *fl, struct file *f)
{
  ASSERT (f != NULL);
  // Create fd_node to store file
  struct fd_node *new_fd_entry
      = (struct fd_node *)malloc (sizeof (struct fd_node));
  new_fd_entry->fd = 2;
  new_fd_entry->f = f;
  // Search for the first unallocated fd number.
  struct list_elem *e = list_begin (fl);
  for (; e != list_end (fl)
         && new_fd_entry->fd == list_entry (e, struct fd_node, fd_elem)->fd;
       e = list_next (e), new_fd_entry->fd++)
    ;
  list_insert (e, &new_fd_entry->fd_elem);
  return new_fd_entry->fd;
}

/* Get the fd_node associated with the fd.
  reutrn NULL if the given fd doesn't exist.
*/
struct file *
fd_list_get_file (struct list *fl, int fd)
{
  if (fd < 2)
    {
      return NULL;
    }
  for (struct list_elem *e = list_begin (fl); e != list_end (fl);
       e = list_next (e))
    {
      struct fd_node *node = list_entry (e, struct fd_node, fd_elem);
      if (fd == node->fd)
        {
          return node->f;
        }
    }
  return NULL;
}

/* remove and close the fd */
void
fd_list_remove (struct list *fl, int fd)
{
  // invalid fd, cannot cloes stdin/stdout
  if (fd < 2)
    {
      return;
    }

  for (struct list_elem *e = list_begin (fl); e != list_end (fl);
       e = list_next (e))
    {
      struct fd_node *node = list_entry (e, struct fd_node, fd_elem);
      if (fd == node->fd)
        {
          file_close (node->f);
          list_remove (e);
          free (node);
          return;
        }
    }
}
