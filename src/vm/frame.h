#ifndef VM_FRAME_H
#define VM_FRAME_H
#include "debug.h"
#include "lib/kernel/hash.h"
#include "lib/kernel/list.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/syscall.h"

void vm_frame_init (void);
void *vm_frame_allocate (enum palloc_flags, void *);
void vm_frame_free (void *);
void vm_frame_access (void *);
#endif // VM_FRAME_H
