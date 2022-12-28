#include "filesys/filesys.h"
#include "buffer_cache.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>

static void parse_path (const char *name, char *const path,
                        char *const filename);
struct lock fs_lock;

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format)
{
  buffer_cache_init ();

  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  lock_init (&fs_lock);

  inode_init ();
  free_map_init ();

  if (format)
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void)
{
  buffer_cache_close ();
  free_map_close ();
  buffer_cache_close ();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size, bool is_dir)
{
  lock_acquire (&fs_lock);
  // Seperate filename from path
  char *path = (char *)malloc (sizeof (char) * (strlen (name) + 1));
  char *dirname = (char *)malloc (sizeof (char) * strlen (name) + 1);
  parse_path (name, path, dirname);
  if (strlen (dirname) == 0)
    {
      free (path);
      free (dirname);
      lock_release (&fs_lock);
      return false;
    }

  block_sector_t inode_sector = 0;
  struct dir *dir = dir_open_path (path);
  bool success = (dir != NULL && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size, is_dir, dir)
                  && dir_add (dir, dirname, inode_sector));
  if (!success && inode_sector != 0)
    free_map_release (inode_sector, 1);

  dir_close (dir);
  free (path);
  free (dirname);
  lock_release (&fs_lock);
  return success;
}

/* Test if the path is a normal file or a directory
 */
bool
filesys_isdir (const char *name)
{
  lock_acquire (&fs_lock);
  // Seperate filename from path
  char *path = (char *)malloc (strlen (name) + 1);
  char *filename = (char *)malloc (strlen (name) + 1);
  parse_path (name, path, filename);

  // open the directory.
  struct dir *dir = dir_open_path (path);
  struct inode *inode = NULL;
  if (dir != NULL)
    dir_lookup (dir, filename, &inode);
  dir_close (dir);

  free (path);
  free (filename);
  lock_release (&fs_lock);

  // nothing behind the last /
  // this is definitedly directory
  if (strlen (filename) == 0)
    return true;

  // find result
  if (inode)
    {
      bool isdir = inode_isdir (inode);
      inode_close (inode);
      return isdir;
    }
  return false;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  lock_acquire (&fs_lock);
  // Seperate filename from path
  char *path = (char *)malloc (sizeof (char) * (strlen (name) + 1));
  char *filename = (char *)malloc (sizeof (char) * strlen (name) + 1);
  parse_path (name, path, filename);

  // open the directory.
  struct dir *dir = dir_open_path (path);
  struct inode *inode = NULL;
  if (dir != NULL)
    dir_lookup (dir, filename, &inode);
  dir_close (dir);

  free (path);
  free (filename);
  lock_release (&fs_lock);
  ASSERT (inode == NULL || !inode_isdir (inode));
  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name)
{
  lock_acquire (&fs_lock);

  struct dir *dir = dir_open_root ();
  bool success = dir != NULL && dir_remove (dir, name);
  dir_close (dir);

  lock_release (&fs_lock);
  return success;
}

bool
filesys_mkdir (const char *name)
{
  return filesys_create (name, 16, true);
}

bool
filesys_chdir (const char *dirname)
{
  struct dir *dir = dir_open_path (dirname);
  if (!dir)
    return false;
  struct thread *t = thread_current ();
  if (t->working_directory)
    dir_close (t->working_directory);
  t->working_directory = dir;
  return true;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16, ROOT_DIR_SECTOR))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}

static void
parse_path (const char *name, char *const path, char *const filename)
{
  int name_len = strlen (name);
  strlcpy (path, name, name_len + 1);
  char *dir_end = strrchr (path, '/');
  if (dir_end)
    {
      *dir_end = '\0';
      strlcpy (filename, dir_end + 1, name_len + 1);
    }
  else
    {
      path[0] = '\0';
      strlcpy (filename, name, name_len + 1);
    }
}
