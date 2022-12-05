#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "filesys/fat.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* On-disk inode.
 * Must be exactly DISK_SECTOR_SIZE bytes long. */
struct inode_disk {
	disk_sector_t start;                /* First data sector. */
	off_t length;                       /* File size in bytes. */
	unsigned magic;                     /* Magic number. */
	uint32_t unused[125];               /* Not used. */
};

/* Returns the number of sectors to allocate for an inode SIZE
 * bytes long. */
static inline size_t
bytes_to_sectors (off_t size) {
	return DIV_ROUND_UP (size, DISK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode {
	struct list_elem elem;              /* Element in inode list. */
	disk_sector_t sector;               /* Sector number of disk location. */
	int open_cnt;                       /* Number of openers. */
	bool removed;                       /* True if deleted, false otherwise. */
	int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
	struct inode_disk data;             /* Inode content. */
};

/* Returns the disk sector that contains byte offset POS within
 * INODE.
 * Returns -1 if INODE does not contain data for a byte at offset
 * POS. */
static disk_sector_t
byte_to_sector (const struct inode *inode, off_t pos) {
	ASSERT (inode != NULL);
	int sectors = (pos/DISK_SECTOR_SIZE) + 1;
	int curr_sct = bytes_to_sectors(inode->data.length);
	disk_sector_t start = inode->data.start;
	int cnt = 0;
	cluster_t clst = sector_to_cluster(start);
	cluster_t next_clst;

	if (sectors <= curr_sct)
	{
		while (true)
		{
			if (cnt == sectors - 1)
				return cluster_to_sector(clst);
			next_clst = fat_get(clst);
			clst = next_clst;
			cnt += 1;
		}
	}
	else
	{
		cluster_t t_prev;
		cluster_t t_curr;
		while (true)
		{
			if (cnt >= curr_sct - 1)
			{
				next_clst = fat_create_chain(clst);
				if (!next_clst)
					return -1;
				t_prev = clst;
				t_curr = next_clst;
				clst = next_clst;
				if (curr_sct != 0)
					cnt += 1;
				break;
			}
			next_clst = fat_get(clst);
			clst = next_clst;
			cnt += 1;
		}
		while (true)
		{
			if (cnt == sectors - 1)
			{
				fat_put(clst, EOChain);
				return cluster_to_sector(clst);
			}
			next_clst = fat_create_chain(clst);
			if (!next_clst)
			{
				fat_remove_chain(t_curr, t_prev);
				return -1;
			}
			clst = next_clst;
			cnt += 1;
		}
	}
}

/* List of open inodes, so that opening a single inode twice
 * returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) {
	list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
 * writes the new inode to sector SECTOR on the file system
 * disk.
 * Returns true if successful.
 * Returns false if memory or disk allocation fails. */
bool
inode_create (disk_sector_t sector, off_t length) {
	struct inode_disk *disk_inode = NULL;
	bool success = false;

	ASSERT (length >= 0);

	/* If this assertion fails, the inode structure is not exactly
	 * one sector in size, and you should fix that. */
	ASSERT (sizeof *disk_inode == DISK_SECTOR_SIZE);
	// What if sector == start? (failed to fat_create_chain)
	disk_inode = calloc (1, sizeof *disk_inode);
	if (disk_inode != NULL) {
		size_t sectors = bytes_to_sectors (length);
		disk_inode->length = length;
		disk_inode->magic = INODE_MAGIC;
		
		bool chain_succ = true;
		cluster_t clst = sector_to_cluster(sector);
		cluster_t pclst;
		cluster_t cclst = clst;
		cluster_t nclst;
		int cnt = 0;

		while (cnt <= sectors)
		{
			nclst = fat_create_chain(cclst);
			if (!nclst)
			{
				chain_succ = false;
				fat_remove_chain(clst, 0);
				break;
			}
			pclst = cclst;
			cclst = nclst;
			cnt += 1;
		}
		disk_inode->start = cluster_to_sector(fat_get(clst));

		if (chain_succ) {
			disk_write (filesys_disk, sector, disk_inode);
			if (sectors > 0) {
				static char zeros[DISK_SECTOR_SIZE];
				disk_sector_t start = disk_inode->start;

				for (int i = 0; i < sectors; i++)
				{
					disk_write(filesys_disk, start, zeros);
					start = cluster_to_sector(fat_get(sector_to_cluster(start)));
				}
			}
			success = true; 
		} 
		free (disk_inode);
	}
	return success;
}

/* Reads an inode from SECTOR
 * and returns a `struct inode' that contains it.
 * Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (disk_sector_t sector) {
	struct list_elem *e;
	struct inode *inode;

	/* Check whether this inode is already open. */
	for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
			e = list_next (e)) {
		inode = list_entry (e, struct inode, elem);
		if (inode->sector == sector) {
			inode_reopen (inode);
			return inode; 
		}
	}

	/* Allocate memory. */
	inode = malloc (sizeof *inode);
	if (inode == NULL)
		return NULL;

	/* Initialize. */
	list_push_front (&open_inodes, &inode->elem);
	inode->sector = sector;
	inode->open_cnt = 1;
	inode->deny_write_cnt = 0;
	inode->removed = false;
	disk_read (filesys_disk, inode->sector, &inode->data);
	return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode) {
	if (inode != NULL)
		inode->open_cnt++;
	return inode;
}

/* Returns INODE's inode number. */
disk_sector_t
inode_get_inumber (const struct inode *inode) {
	return inode->sector;
}

/* Closes INODE and writes it to disk.
 * If this was the last reference to INODE, frees its memory.
 * If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) {
	/* Ignore null pointer. */
	if (inode == NULL)
		return;

	/* Release resources if this was the last opener. */
	if (--inode->open_cnt == 0) {
		/* Remove from inode list and release lock. */
		list_remove (&inode->elem);
		//inode_remove(inode);
		
		/* Write back to disk */
		int size = inode->data.length;


		/* Deallocate blocks if removed. */
		if (inode->removed) {
			fat_remove_chain (inode->sector, 0); 
		}

		free (inode); 
	}
}

/* Marks INODE to be deleted when it is closed by the last caller who
 * has it open. */
void
inode_remove (struct inode *inode) {
	ASSERT (inode != NULL);
	inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
 * Returns the number of bytes actually read, which may be less
 * than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) {
	uint8_t *buffer = buffer_;
	off_t bytes_read = 0;
	uint8_t *bounce = NULL;

	while (size > 0) {
		/* Disk sector to read, starting byte offset within sector. */
		disk_sector_t sector_idx = byte_to_sector (inode, offset);
		int sector_ofs = offset % DISK_SECTOR_SIZE;

		/* Bytes left in inode, bytes left in sector, lesser of the two. */
		off_t inode_left = inode_length (inode) - offset;
		int sector_left = DISK_SECTOR_SIZE - sector_ofs;
		int min_left = inode_left < sector_left ? inode_left : sector_left;

		/* Number of bytes to actually copy out of this sector. */
		int chunk_size = size < min_left ? size : min_left;
		if (chunk_size <= 0)
			break;

		if (sector_ofs == 0 && chunk_size == DISK_SECTOR_SIZE) {
			/* Read full sector directly into caller's buffer. */
			disk_read (filesys_disk, sector_idx, buffer + bytes_read); 
		} else {
			/* Read sector into bounce buffer, then partially copy
			 * into caller's buffer. */
			if (bounce == NULL) {
				bounce = malloc (DISK_SECTOR_SIZE);
				if (bounce == NULL)
					break;
			}
			disk_read (filesys_disk, sector_idx, bounce);
			memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
		}

		/* Advance. */
		size -= chunk_size;
		offset += chunk_size;
		bytes_read += chunk_size;
	}
	free (bounce);

	return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
 * Returns the number of bytes actually written, which may be
 * less than SIZE if end of file is reached or an error occurs.
 * (Normally a write at end of file would extend the inode, but
 * growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
		off_t offset) {
	const uint8_t *buffer = buffer_;
	off_t bytes_written = 0;
	uint8_t *bounce = NULL;

	if (inode->deny_write_cnt)
		return 0;

	size_t added_size = 0;
	cluster_t clst = sector_to_cluster(inode->data.start);
	while(fat_get(clst) != 0 && fat_get(clst) != EOChain){
		clst = fat_get(clst);
		added_size++;
	}
	if(inode_length(inode) < offset + size){
		size_t added_sectors = bytes_to_sectors(offset + size) - added_size;
		for(size_t i = 0; i < added_sectors; i++){
			clst = fat_create_chain(clst);
		}
		inode->data.length = offset + size;
		disk_write(filesys_disk, inode->sector, &inode->data);
	}

	while (size > 0) {
		/* Sector to write, starting byte offset within sector. */
		disk_sector_t sector_idx = byte_to_sector (inode, offset);
		int sector_ofs = offset % DISK_SECTOR_SIZE;

		/* Bytes left in inode, bytes left in sector, lesser of the two. */
		off_t inode_left = inode_length (inode) - offset;
		int sector_left = DISK_SECTOR_SIZE - sector_ofs;
		int min_left = inode_left < sector_left ? inode_left : sector_left;
		int zero_pad = 0;
		/* Number of bytes to actually write into this sector. */
		if (inode_left < size)
		{
			int t_size = size < sector_left ? size : sector_left;
			zero_pad = offset - inode_length(inode);
			inode->data.length = t_size + offset;
			inode_left = inode_length (inode) - offset;
			min_left = inode_left < sector_left ? inode_left : sector_left;
			disk_write(filesys_disk, inode->sector, &inode->data);
		}
		int chunk_size = size < min_left ? size : min_left;
		if (chunk_size <= 0)
			break;
		if (zero_pad)
		{
			uint8_t *t_bounce = NULL;
			off_t ofs = offset - zero_pad;
			while (zero_pad > 0)
			{
				if (t_bounce == NULL) {
					t_bounce = malloc(DISK_SECTOR_SIZE);
					if (t_bounce == NULL)
						break;
				}
				off_t pad_size = zero_pad < sector_left ? zero_pad : sector_left;
				disk_read(filesys_disk, byte_to_sector(inode, ofs), t_bounce);
				memset(t_bounce + ofs, 0, pad_size);
				zero_pad -= pad_size;
				ofs += pad_size;
			}
		}
		if (sector_ofs == 0 && chunk_size == DISK_SECTOR_SIZE) {
			/* Write full sector directly to disk. */
			disk_write (filesys_disk, sector_idx, buffer + bytes_written); 
		} else {
			/* We need a bounce buffer. */
			if (bounce == NULL) {
				bounce = malloc (DISK_SECTOR_SIZE);
				if (bounce == NULL)
					break;
			}

			/* If the sector contains data before or after the chunk
			   we're writing, then we need to read in the sector
			   first.  Otherwise we start with a sector of all zeros. */
			if (sector_ofs > 0 || chunk_size < sector_left) 
				disk_read (filesys_disk, sector_idx, bounce);
			else
				memset (bounce, 0, DISK_SECTOR_SIZE);
			memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
			disk_write (filesys_disk, sector_idx, bounce); 
		}

		/* Advance. */
		size -= chunk_size;
		offset += chunk_size;
		bytes_written += chunk_size;
	}
	free (bounce);
	
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
 * Must be called once by each inode opener who has called
 * inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) {
	ASSERT (inode->deny_write_cnt > 0);
	ASSERT (inode->deny_write_cnt <= inode->open_cnt);
	inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode) {
	return inode->data.length;
}
