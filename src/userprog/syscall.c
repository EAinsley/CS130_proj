#include "userprog/syscall.h"
#include "devices/shutdown.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include <stdio.h>
#include <syscall-nr.h>

static void syscall_handler (struct intr_frame *);

void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/* the syscall function prototypes */

#define FN(name) sys__##name

/* Projects 2 and later. */
static void FN (halt) (void);
static void FN (exit) (int status);
static tid_t FN (exec) (const char *cmd_line);
static int FN (wait) (tid_t pid);
static bool FN (create) (const char *file, unsigned initial_size);
static bool FN (remove) (const char *file);
static int FN (open) (const char *file);
static int FN (filesize) (int fd);
static int FN (read) (int fd, void *buffer, unsigned size);
static int FN (write) (int fd, const void *buffer, unsigned size);
static void FN (seek) (int fd, unsigned position);
static unsigned FN (tell) (int fd);
static void FN (close) (int fd);

// macros for extracting syscall argument
#define syscall_arg(frame, idx, type) (*(type *)((char *)frame->esp + 4 * idx))
#define ARG1(t1) syscall_arg (f, 1, t1)
#define ARG2(t2) syscall_arg (f, 2, t2)
#define ARG3(t3) syscall_arg (f, 3, t3)

// macros for forwarding syscalls
#define FWD0(fn) FN (fn) ()
#define FWD1(fn, t1) FN (fn) (ARG1 (t1))
#define FWD2(fn, t1, t2) FN (fn) (ARG1 (t1), ARG2 (t2))
#define FWD3(fn, t1, t2, t3) FN (fn) (ARG1 (t1), ARG2 (t2), ARG3 (t3))

#define FWD_CASE(nr, todo)                                                    \
  do                                                                          \
    if (id == nr)                                                             \
      todo;                                                                   \
  while (0);

static void
syscall_handler (struct intr_frame *f UNUSED)
{
  int id = *(int *)f->esp;
  /* Projects 2 and later. */
  FWD_CASE (SYS_HALT, FWD0 (halt));
  FWD_CASE (SYS_EXIT, FWD1 (exit, int));
  FWD_CASE (SYS_EXEC, FWD1 (exec, const char *));
  FWD_CASE (SYS_WAIT, FWD1 (wait, tid_t));
  FWD_CASE (SYS_CREATE, FWD2 (create, const char *, unsigned));
  FWD_CASE (SYS_REMOVE, FWD1 (remove, const char *));
  FWD_CASE (SYS_OPEN, FWD1 (open, const char *));
  FWD_CASE (SYS_FILESIZE, FWD1 (filesize, int));
  FWD_CASE (SYS_READ, FWD3 (read, int, void *, unsigned));
  FWD_CASE (SYS_WRITE, FWD3 (write, int, const void *, unsigned));
  FWD_CASE (SYS_SEEK, FWD2 (seek, int, unsigned));
  FWD_CASE (SYS_TELL, FWD1 (tell, int));
  FWD_CASE (SYS_CLOSE, FWD1 (close, int));
}

/*
  implementation section
*/

static void
FN (halt) (void)
{
  shutdown_power_off ();
}
static void
FN (exit) (int status)
{
  struct proc_record *proc = thread_current ()->proc;
  if (proc != NULL)
    proc->proc_status = status;
  thread_exit ();
}
static tid_t
FN (exec) (const char *cmd_line UNUSED)
{
  // TODO
  return 0;
}
static int
FN (wait) (tid_t pid)
{
  return process_wait (pid);
}
static bool
FN (create) (const char *file UNUSED, unsigned initial_size UNUSED)
{
  // TODO
  return false;
}
static bool
FN (remove) (const char *file UNUSED)
{
  // TODO
  return false;
}
static int
FN (open) (const char *file UNUSED)
{
  // TODO
  return 0;
}
static int
FN (filesize) (int fd UNUSED)
{
  // TODO
  return 0;
}
static int
FN (read) (int fd UNUSED, void *buffer UNUSED, unsigned size UNUSED)
{
  // TODO
  return 0;
}
static int
FN (write) (int fd, const void *buffer, unsigned size)
{
  const char *buf = (const char *)buffer;
  // temporarily added for arg-pass
  if (fd == 1)
    {
      for (unsigned i = 0; i < size; i++)
        putchar (buf[i]);
      return 0;
    }
  // TODO
  return 0;
}
static void
FN (seek) (int fd UNUSED, unsigned position UNUSED)
{
  // TODO
}
static unsigned
FN (tell) (int fd UNUSED)
{
  // TODO
  return 0;
}
static void
FN (close) (int fd UNUSED)
{
  // TODO
}
