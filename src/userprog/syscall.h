#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

typedef int mapid_t;

void syscall_init (void);
/* process exit on error */
void err_exit (void);
#endif /* userprog/syscall.h */
