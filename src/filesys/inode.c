#include "filesys/inode.h"
#include "buffer_cache.h"
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include <debug.h>
#include <list.h>
#include <round.h>
#include <string.h>

/* zero filled sector */
static char zeros[BLOCK_SECTOR_SIZE];

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* filesys partition sector index: 8MB = 2^14 SECTORS, use 16bit=2B to store
 * the index*/
typedef uint16_t fs_sec_t;
#define PTR_SIZE (sizeof (fs_sec_t))
/* store up to 256 pointers in one indirect block */
#define PTR_PER_SEC (BLOCK_SECTOR_SIZE / PTR_SIZE)

#define FS_SIZE (8 * (1 << 20))
#define FS_SECTORS (FS_SIZE / BLOCK_SECTOR_SIZE)
#define ERR_SECTOR ((fs_sec_t)(-1))
#define INDIRECT_COUNT (FS_SECTORS / PTR_PER_SEC)

// helper functions
static fs_sec_t allocate_sector (void);
static void release_sector (fs_sec_t);
static fs_sec_t allocate_indirect (fs_sec_t);
static void release_indirect (fs_sec_t, fs_sec_t);

/* On-disk indirect block layout: index 256 data sectors */
struct indirect_block
{
  fs_sec_t data_sectors[PTR_PER_SEC];
};

/* On-disk inode.  Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
{
  fs_sec_t indirect_blocks[INDIRECT_COUNT];
  off_t length;                /* File size in bytes. */
  bool isdir;                  /* this inode is a normal file or a directory */
  unsigned magic;              /* Magic number. */
  char pad[BLOCK_SECTOR_SIZE]; /* make sure the disk inode is big enough */
};

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode
{
  struct list_elem elem;  /* Element in inode list. */
  struct lock mutex;      /* for synchronization */
  block_sector_t sector;  /* Sector number of disk location. */
  int open_cnt;           /* Number of openers. */
  bool removed;           /* True if deleted, false otherwise. */
  int deny_write_cnt;     /* 0: writes ok, >0: deny writes. */
  struct inode_disk data; /* Inode content. */
};

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos)
{
  ASSERT (inode != NULL);
  if (pos >= inode_length (inode))
    return -1;
  // index calculation
  ASSERT ((pos / BLOCK_SECTOR_SIZE) < FS_SECTORS)
  fs_sec_t sec_off = (fs_sec_t)(pos / BLOCK_SECTOR_SIZE);
  fs_sec_t ind_blk = sec_off / PTR_PER_SEC;
  fs_sec_t ind_idx = sec_off % PTR_PER_SEC;
  // read the indirect block from disk
  struct indirect_block ind_data;
  ASSERT (inode->data.indirect_blocks[ind_blk] != ERR_SECTOR);
  buffer_cache_read (inode->data.indirect_blocks[ind_blk], (void *)&ind_data,
                     0, BLOCK_SECTOR_SIZE);
  // find the corresponding sector
  ASSERT (ind_data.data_sectors[ind_idx] != ERR_SECTOR);
  return (block_sector_t)ind_data.data_sectors[ind_idx];
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void)
{
  list_init (&open_inodes);
  ASSERT (sizeof (struct inode_disk) >= BLOCK_SECTOR_SIZE);
}

/* Get a free sector and zero it, return the sector index.
   Return ERR_SECTOR on error
*/
static fs_sec_t
allocate_sector ()
{
  block_sector_t sec = 0;
  if (!free_map_allocate (1, &sec))
    return ERR_SECTOR;
  buffer_cache_write (sec, zeros, 0, BLOCK_SECTOR_SIZE);
  return (fs_sec_t)sec;
}
/* Free one disk sector */
static void
release_sector (fs_sec_t sec)
{
  free_map_release (sec, 1);
}

/* Allocate a indirect block on the disk,
   with data_blks underlying data blocks.
   Return the sector index of the indirect block.
   Return ERR_SECTOR on error.
*/
static fs_sec_t
allocate_indirect (fs_sec_t data_blks)
{
  ASSERT (data_blks <= PTR_PER_SEC);
  fs_sec_t ind_sec = allocate_sector ();
  // error: can not allocate the indirect block
  if (ind_sec == ERR_SECTOR)
    return ERR_SECTOR;

  struct indirect_block ind_blk;
  for (fs_sec_t i = 0; i < PTR_PER_SEC; i++)
    ind_blk.data_sectors[i] = ERR_SECTOR;
  for (fs_sec_t i = 0; i < data_blks; i++)
    {
      fs_sec_t sec = allocate_sector ();
      // no more space:
      // 1. release previously allocated data blocks
      // 2. also release the indirect data block
      // 3. return error sector number
      if (sec == ERR_SECTOR)
        {
          for (fs_sec_t j = 0; j < i; j++)
            release_sector (ind_blk.data_sectors[j]);
          release_sector (ind_sec);
          return ERR_SECTOR;
        }
      ind_blk.data_sectors[i] = sec;
    }
  buffer_cache_write (ind_sec, (void *)&ind_blk, 0, BLOCK_SECTOR_SIZE);
  return ind_sec;
}

/* release a indirect block on the disk,
   with data_blks underlying data blocks */
static void
release_indirect (fs_sec_t ind_sec, fs_sec_t data_blks)
{
  struct indirect_block ind_blk;
  buffer_cache_read ((block_sector_t)ind_sec, (void *)&ind_blk, 0,
                     BLOCK_SECTOR_SIZE);
  for (fs_sec_t i = 0; i < data_blks; i++)
    release_sector (ind_blk.data_sectors[i]);
  release_sector (ind_sec);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t inode_sector, off_t length)
{
  ASSERT (length >= 0);

  struct inode_disk disk_inode;
  memset ((void *)&disk_inode, 0, sizeof (struct inode_disk));
  for (fs_sec_t i = 0; i < INDIRECT_COUNT; i++)
    disk_inode.indirect_blocks[i] = ERR_SECTOR;
  disk_inode.length = length;
  disk_inode.magic = INODE_MAGIC;

  // number of required sectors, required indirect blocks
  fs_sec_t sectors = (fs_sec_t)bytes_to_sectors (length);
  fs_sec_t indirects = (fs_sec_t)DIV_ROUND_UP (sectors, PTR_PER_SEC);
  // allocate the indirect blocks
  for (fs_sec_t i = 0; i < indirects; i++)
    {
      fs_sec_t data_blks = sectors >= PTR_PER_SEC ? PTR_PER_SEC : sectors;
      fs_sec_t ind_blk_sec = allocate_indirect (data_blks);
      // error: release previously allocated indirect blocks
      if (ind_blk_sec == ERR_SECTOR)
        {
          for (fs_sec_t j = 0; j < i; j++)
            release_indirect (disk_inode.indirect_blocks[j], PTR_PER_SEC);
          return false;
        }
      disk_inode.indirect_blocks[i] = ind_blk_sec;
      sectors -= data_blks;
    }
  buffer_cache_write (inode_sector, (void *)&disk_inode, 0, BLOCK_SECTOR_SIZE);
  return true;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e))
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector)
        {
          inode_reopen (inode);
          return inode;
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  lock_init (&inode->mutex);
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  buffer_cache_read (inode->sector, &inode->data, 0, BLOCK_SECTOR_SIZE);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode)
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);

      /* Deallocate blocks if removed. */
      if (inode->removed)
        {
          // number of data blocks and indirect blocks in the inode
          fs_sec_t sectors = (fs_sec_t)bytes_to_sectors (inode->data.length);
          fs_sec_t indirects = (fs_sec_t)DIV_ROUND_UP (sectors, PTR_PER_SEC);
          // release the indirect block sectors
          for (fs_sec_t i = 0; i < indirects; i++)
            {
              fs_sec_t data_blks
                  = sectors >= PTR_PER_SEC ? PTR_PER_SEC : sectors;
              release_indirect (inode->data.indirect_blocks[i], data_blks);
              sectors -= data_blks;
            }
          // release the inode block sector
          release_sector (inode->sector);
        }
      free (inode);
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode)
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset)
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  lock_acquire (&inode->mutex);

  while (size > 0)
    {
      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      buffer_cache_read (sector_idx, buffer + bytes_read, sector_ofs,
                         chunk_size);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }

  lock_release (&inode->mutex);
  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset)
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;

  if (inode->deny_write_cnt)
    return 0;

  lock_acquire (&inode->mutex);
  // extend the file
  if (offset + size > inode_length (inode))
    {
      for (off_t i = inode_length (inode); i < offset + size; i++)
        {
          fs_sec_t sec_off = (fs_sec_t)(i / BLOCK_SECTOR_SIZE);
          fs_sec_t ind_blk = sec_off / PTR_PER_SEC;
          fs_sec_t ind_idx = sec_off % PTR_PER_SEC;
          // new indirect block is required
          if (inode->data.indirect_blocks[ind_blk] == ERR_SECTOR)
            {
              fs_sec_t ind_sec = allocate_indirect (1);
              // FIXME - what to do if we cannot further extend the file
              inode->data.indirect_blocks[ind_blk] = ind_sec;
            }

          struct indirect_block ind_data;
          buffer_cache_read (inode->data.indirect_blocks[ind_blk],
                             (void *)&ind_data, 0, BLOCK_SECTOR_SIZE);
          // new data block is required
          if (ind_data.data_sectors[ind_idx] == ERR_SECTOR)
            {
              fs_sec_t data_sec = allocate_sector ();
              // FIXME - what to do if we cannot further extend the file
              ind_data.data_sectors[ind_idx] = data_sec;
              buffer_cache_write (inode->data.indirect_blocks[ind_blk],
                                  (void *)&ind_data, 0, BLOCK_SECTOR_SIZE);
            }
        }

      // update inode metadata
      inode->data.length = offset + size;
      buffer_cache_write (inode->sector, (void *)&inode->data, 0,
                          BLOCK_SECTOR_SIZE);
    }
  while (size > 0)
    {
      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);

      /* Write the sector to the cache */
      buffer_cache_write (sector_idx, buffer + bytes_written, sector_ofs,
                          chunk_size);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }

  lock_release (&inode->mutex);
  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode)
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode)
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}
