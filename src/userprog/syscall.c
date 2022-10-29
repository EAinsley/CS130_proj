#include "userprog/syscall.h"
#include "devices/shutdown.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include <stdio.h>
#include <syscall-nr.h>

#define SYSCALL_FN(name) sys__##name

/* process exit on error */
static void err_exit (void);

/* Projects 2 and later. */
static void SYSCALL_FN (halt) (void);
static void SYSCALL_FN (exit) (int status);
static tid_t SYSCALL_FN (exec) (const char *cmd_line);
static int SYSCALL_FN (wait) (tid_t pid);
static bool SYSCALL_FN (create) (const char *file, unsigned initial_size);
static bool SYSCALL_FN (remove) (const char *file);
static int SYSCALL_FN (open) (const char *file);
static int SYSCALL_FN (filesize) (int fd);
static int SYSCALL_FN (read) (int fd, void *buffer, unsigned size);
static int SYSCALL_FN (write) (int fd, const void *buffer, unsigned size);
static void SYSCALL_FN (seek) (int fd, unsigned position);
static unsigned SYSCALL_FN (tell) (int fd);
static void SYSCALL_FN (close) (int fd);

/* Reads a byte at user virtual address UADDR.
   UADDR must be below PHYS_BASE.
   Returns the byte value if successful, -1 if a segfault
   occurred. */
static int32_t
get_user (const uint8_t *uaddr)
{
  // accessing a kernel space memory, reject
  if (!((void *)uaddr < PHYS_BASE))
    return -1;

  int result;
  asm("movl $1f, %0; movzbl %1, %0; 1:" : "=&a"(result) : "m"(*uaddr));
  return result;
}

/* Writes BYTE to user address UDST.
   UDST must be below PHYS_BASE.
   Returns true if successful, false if a segfault occurred. */
static bool
put_user (uint8_t *udst, uint8_t byte)
{
  int error_code;
  asm("movl $1f, %0; movb %b2, %1; 1:"
      : "=&a"(error_code), "=m"(*udst)
      : "q"(byte));
  return error_code != -1;
}

/* process exit on error */
static void
err_exit ()
{
  thread_current ()->proc->abnormal_exit = true;
  SYSCALL_FN (exit) (-1);
}

static uint32_t
_syscall_arg (struct intr_frame *f, uint32_t idx)
{
  uint32_t arg = 0;
  uint8_t *p_arg = (uint8_t *)&arg, *p_usr = f->esp + idx * 4;
  // every argument is of 4 bytes
  for (int i = 0; i < 4; i++, p_arg++, p_usr++)
    {
      int32_t tmp = get_user (p_usr);
      // invalid argument address, terminate the process
      if (tmp == -1)
        err_exit ();
      *p_arg = (uint8_t)tmp;
    }
  return arg;
}

// macros for extracting syscall argument
#define syscall_arg(frame, idx, type) ((type)_syscall_arg (frame, idx))

#define ARG1(t1) syscall_arg (f, 1, t1)
#define ARG2(t2) syscall_arg (f, 2, t2)
#define ARG3(t3) syscall_arg (f, 3, t3)

// macros for forwarding syscalls
#define FWD0(fn)                                                              \
  do                                                                          \
    {                                                                         \
      SYSCALL_FN (fn) ();                                                     \
      return;                                                                 \
    }                                                                         \
  while (0)
#define FWD1(fn, t1)                                                          \
  do                                                                          \
    {                                                                         \
      SYSCALL_FN (fn) (ARG1 (t1));                                            \
      return;                                                                 \
    }                                                                         \
  while (0)
#define FWD2(fn, t1, t2)                                                      \
  do                                                                          \
    {                                                                         \
      SYSCALL_FN (fn) (ARG1 (t1), ARG2 (t2));                                 \
      return;                                                                 \
    }                                                                         \
  while (0)
#define FWD3(fn, t1, t2, t3)                                                  \
  do                                                                          \
    {                                                                         \
      SYSCALL_FN (fn) (ARG1 (t1), ARG2 (t2), ARG3 (t3));                      \
      return;                                                                 \
    }                                                                         \
  while (0)

// macros for forwarding syscalls, with return value
#define FWD0_RET(fn)                                                          \
  do                                                                          \
    {                                                                         \
      f->eax = (uint32_t)(SYSCALL_FN (fn) ());                                \
      return;                                                                 \
    }                                                                         \
  while (0)
#define FWD1_RET(fn, t1)                                                      \
  do                                                                          \
    {                                                                         \
      f->eax = (uint32_t)(SYSCALL_FN (fn) (ARG1 (t1)));                       \
      return;                                                                 \
    }                                                                         \
  while (0)
#define FWD2_RET(fn, t1, t2)                                                  \
  do                                                                          \
    {                                                                         \
      f->eax = (uint32_t)(SYSCALL_FN (fn) (ARG1 (t1), ARG2 (t2)));            \
      return;                                                                 \
    }                                                                         \
  while (0)
#define FWD3_RET(fn, t1, t2, t3)                                              \
  do                                                                          \
    {                                                                         \
      f->eax = (uint32_t)(SYSCALL_FN (fn) (ARG1 (t1), ARG2 (t2), ARG3 (t3))); \
      return;                                                                 \
    }                                                                         \
  while (0)

// generate if else case
#define FWD_CASE(nr, todo)                                                    \
  do                                                                          \
    if (id == nr)                                                             \
      todo;                                                                   \
  while (0)

static void
syscall_handler (struct intr_frame *f)
{
  int id = syscall_arg (f, 0, int);
  /* Projects 2 and later. */
  FWD_CASE (SYS_HALT, FWD0 (halt));
  FWD_CASE (SYS_EXIT, FWD1 (exit, int));
  FWD_CASE (SYS_EXEC, FWD1_RET (exec, const char *));
  FWD_CASE (SYS_WAIT, FWD1_RET (wait, tid_t));
  FWD_CASE (SYS_CREATE, FWD2_RET (create, const char *, unsigned));
  FWD_CASE (SYS_REMOVE, FWD1_RET (remove, const char *));
  FWD_CASE (SYS_OPEN, FWD1_RET (open, const char *));
  FWD_CASE (SYS_FILESIZE, FWD1_RET (filesize, int));
  FWD_CASE (SYS_READ, FWD3_RET (read, int, void *, unsigned));
  FWD_CASE (SYS_WRITE, FWD3_RET (write, int, const void *, unsigned));
  FWD_CASE (SYS_SEEK, FWD2 (seek, int, unsigned));
  FWD_CASE (SYS_TELL, FWD1_RET (tell, int));
  FWD_CASE (SYS_CLOSE, FWD1 (close, int));

  // invalid system call
  err_exit ();
}
void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/*
  implementation section
*/

static void
SYSCALL_FN (halt) (void)
{
  shutdown_power_off ();
}
static void
SYSCALL_FN (exit) (int status)
{
  struct proc_record *proc = thread_current ()->proc;
  if (proc != NULL)
    proc->proc_status = status;
  thread_exit ();
}
static tid_t
SYSCALL_FN (exec) (const char *cmd_line UNUSED)
{
  // TODO: user memory access checking
  return process_execute (cmd_line);
}
static int
SYSCALL_FN (wait) (tid_t pid)
{
  return process_wait (pid);
}
static bool
SYSCALL_FN (create) (const char *file UNUSED, unsigned initial_size UNUSED)
{
  // TODO
  return false;
}
static bool
SYSCALL_FN (remove) (const char *file UNUSED)
{
  // TODO
  return false;
}
static int
SYSCALL_FN (open) (const char *file UNUSED)
{
  // TODO
  return 0;
}
static int
SYSCALL_FN (filesize) (int fd UNUSED)
{
  // TODO
  return 0;
}
static int
SYSCALL_FN (read) (int fd UNUSED, void *buffer UNUSED, unsigned size UNUSED)
{
  // TODO
  return 0;
}
static int
SYSCALL_FN (write) (int fd, const void *buffer, unsigned size)
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
SYSCALL_FN (seek) (int fd UNUSED, unsigned position UNUSED)
{
  // TODO
}
static unsigned
SYSCALL_FN (tell) (int fd UNUSED)
{
  // TODO
  return 0;
}
static void
SYSCALL_FN (close) (int fd UNUSED)
{
  // TODO
}
