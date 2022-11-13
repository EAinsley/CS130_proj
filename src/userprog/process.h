#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "filesys/file.h"
#include "lib/kernel/list.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"

typedef int tid_t;

#define MAX_CHS 64 // maximum child processes of a process

enum proc_status
{
  PROC_RUNNING,
  PROC_NORMAL_EXIT,
  PROC_ERROR_EXIT
};

/* the process control block except the pagedir */
struct proc_record
{
  tid_t id;                              // thread id of this process
  int exit_code;                         // process status code
  enum proc_status proc_status;          // the reason for process termination
  struct semaphore sema_exit;            // up on exit, indicates proc exiting
  bool orphan;                           // whether the parent process exists
  struct proc_record *children[MAX_CHS]; // children process records
  struct list fd_list; // list used to record file descriptors.
  struct file *image;  // the image of self.
};

/* find the process whose thread id equals to the given id and return the
 * process record */
struct proc_record *proc_find (tid_t id);
/* initialize a process record */
void proc_init (struct proc_record *proc);
/* Find a child process with the given id.  */
struct proc_record *proc_find_child (tid_t id);
/* Find currently running process*/
struct proc_record *proc_current (void);
/* add a child process into the children filed of parent */
void proc_add_child (struct proc_record *proc, struct proc_record *child);
/* add a new child process id to the children list of current thread */
void proc_remove_child (struct proc_record *child_proc);

// The struct to record the fd and file mapping.
struct fd_node
{
  int fd;         // file descriptor
  struct file *f; // file
  struct list_elem fd_elem;
};

/* clear the fd_list*/
void fd_list_clear (struct list *fdlist);
/* Insert new files and get proper fd*/
int fd_list_insert (struct list *fl, struct file *f);
/* Get the file with specified fd*/
struct file *fd_list_get_file (struct list *ls, int fd);
/* close and remove the fd, return the file */
void fd_list_remove (struct list *ls, int fd);

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

#endif /* userprog/process.h */
