#include "userprog/syscall.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include <stdio.h>
#include <syscall-nr.h>

#define syscall_arg(frame, idx, type) (*(type *)((char *)frame->esp + 4 * idx))

static void syscall_handler (struct intr_frame *);

void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

// int syscall_write (int fd, const void *buffer, unsigned size);

static void
syscall_handler (struct intr_frame *f UNUSED)
{
  // temporarily added for arg-pass
  if (*(int *)f->esp == SYS_WRITE)
    {
      int fd = syscall_arg (f, 1, int);
      char *buf = syscall_arg (f, 2, char *);
      unsigned size = syscall_arg (f, 3, unsigned);

      if (fd == 1)
        {
          for (unsigned i = 0; i < size; i++)
            putchar (buf[i]);
          return;
        }
    }

  // printf ("system call!\n");
  thread_exit ();
}
