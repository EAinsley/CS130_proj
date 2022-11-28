#ifndef VM_FRAME_H
#define VM_FRAME_H
#include "lib/kernel/hash.h"
#include "lib/kernel/list.h"
#include "threads/palloc.h"
#include "threads/synch.h"
void vm_frame_init (void);
void vm_frame_allocate (enum palloc_flags);
void vm_frame_free (void *);
void vm_frame_access (void *);
#endif // VM_FRAME_H
