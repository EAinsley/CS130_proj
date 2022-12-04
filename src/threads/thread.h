#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include "userprog/process.h"
#include "vm/sup_page.h"
#include <debug.h>
#include <list.h>
#include <stdint.h>
typedef int mapid_t;
/* States in a thread's life cycle. */
enum thread_status
{
  THREAD_RUNNING, /* Running thread. */
  THREAD_READY,   /* Not running but ready to run. */
  THREAD_BLOCKED, /* Waiting for an event to trigger. */
  THREAD_DYING    /* About to be destroyed. */
};

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t)-1) /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0      /* Lowest priority. */
#define PRI_DEFAULT 31 /* Default priority. */
#define PRI_MAX 63     /* Highest priority. */

/* A kernel thread or user process.

   Each thread structure is stored in its own 4 kB page.  The
   thread structure itself sits at the very bottom of the page
   (at offset 0).  The rest of the page is reserved for the
   thread's kernel stack, which grows downward from the top of
   the page (at offset 4 kB).  Here's an illustration:

        4 kB +---------------------------------+
             |          kernel stack           |
             |                |                |
             |                |                |
             |                V                |
             |         grows downward          |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             +---------------------------------+
             |              magic              |
             |                :                |
             |                :                |
             |               name              |
             |              status             |
        0 kB +---------------------------------+

   The upshot of this is twofold:

      1. First, `struct thread' must not be allowed to grow too
         big.  If it does, then there will not be enough room for
         the kernel stack.  Our base `struct thread' is only a
         few bytes in size.  It probably should stay well under 1
         kB.

      2. Second, kernel stacks must not be allowed to grow too
         large.  If a stack overflows, it will corrupt the thread
         state.  Thus, kernel functions should not allocate large
         structures or arrays as non-static local variables.  Use
         dynamic allocation with malloc() or palloc_get_page()
         instead.

   The first symptom of either of these problems will probably be
   an assertion failure in thread_current(), which checks that
   the `magic' member of the running thread's `struct thread' is
   set to THREAD_MAGIC.  Stack overflow will normally change this
   value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
   the run queue (thread.c), or it can be an element in a
   semaphore wait list (synch.c).  It can be used these two ways
   only because they are mutually exclusive: only a thread in the
   ready state is on the run queue, whereas only a thread in the
   blocked state is on a semaphore wait list. */
struct thread
{
  /* Owned by thread.c. */
  tid_t tid;                 /* Thread identifier. */
  enum thread_status status; /* Thread state. */
  char name[16];             /* Name (for debugging purposes). */
  uint8_t *stack;            /* Saved stack pointer. */
  int priority;              /* Priority. */
  struct list_elem allelem;  /* List element for all threads list. */

  /* Shared between thread.c and synch.c. */
  struct list_elem elem; /* List element. */

  /* Owned by timer.c */
  struct list_elem sleepelem; /* List element for sleep thread queue*/
  int64_t sleep_to;           /* sleep until `sleep_tp` ticks */

#ifdef USERPROG
  /* Owned by userprog/process.c. */

  uint32_t *pagedir; // Page directory.
  /* created on process_execute.
     the parent proecss deallocate it for the children
  */
  struct proc_record *proc; // data records for process control
  /*
  Syscall handler may trigger page fault in kernel-mode.
  In this case, page fault interrupt frame ESP is not the user-prog ESP but the
  kernel stack pointer. To correctly handle the case and grow the stack, we
  have to save ESP in syscall handler.
  */
  void *userprog_syscall_esp; // save the ESP stack pointer for page-fault
                              // handling
#endif

#ifdef VM
  struct vm_sup_page_table *supplemental_table;
  struct list mmap_list; // list used to record mmap information
#endif

  /* Owned by thread.c. */
  unsigned magic; /* Detects stack overflow. */
};

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

/* Performs some operation on thread t, given auxiliary data AUX. */
typedef void thread_action_func (struct thread *t, void *aux);
void thread_foreach (thread_action_func *, void *);

int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

/* find a process by thread id */
struct thread *thread_find (tid_t id);

struct thread_mmap_node
{
  mapid_t mapid;    // The allocated mapid
  void *upage_addr; // The start upage addr the file maps to
  uint32_t pages_count;
  struct list_elem list_elem;
};

/* clear the mmap list, release all the resources */
void thread_mmap_list_clear (struct list *mmap_list);
/* Insert new mmap files into the list and return the mapid*/
mapid_t thread_mmap_list_insert (struct list *ml, void *upage_addr,
                                 uint32_t count);
/* unmap the mapped file and free the resource. */
void thread_mmap_list_remove (struct list *ml, mapid_t mapid);
#endif /* threads/thread.h */
