#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "devices/disk.h"
#include "filesys/fat.h"
#include "threads/thread.h"

/* The disk that contains the file system. */
struct disk *filesys_disk;

static void do_format (void);

/* Initializes the file system module.
 * If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) {
	filesys_disk = disk_get (0, 1);
	if (filesys_disk == NULL)
		PANIC ("hd0:1 (hdb) not present, file system initialization failed");

	inode_init ();

#ifdef EFILESYS
	fat_init ();

	if (format)
		do_format ();

	fat_open ();
#else
	/* Original FS */
	free_map_init ();

	if (format)
		do_format ();

	free_map_open ();
#endif
}

/* Shuts down the file system module, writing any unwritten data
 * to disk. */
void
filesys_done (void) {
	/* Original FS */
#ifdef EFILESYS
	fat_close ();
#else
	free_map_close ();
#endif
}

/* Creates a file named NAME with the given INITIAL_SIZE.
 * Returns true if successful, false otherwise.
 * Fails if a file named NAME already exists,
 * or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size){
	bool pathtype = (name[0] == '/') ? true : false;

	char *tmp = (char *)malloc(strlen(name) + 1);
    strlcpy(tmp, name, strlen(name) + 1);

	char *args[32];
	char *token, *save_ptr;
	int token_cnt = 0;
	token = strtok_r(tmp, "/", &save_ptr);
	while(token != NULL){
		args[token_cnt] = token;
		token = strtok_r(NULL, "/", &save_ptr);
		token_cnt++;
	}
	for(; token_cnt < 32; token_cnt++){
		args[token_cnt] = NULL;
	}
	
	if(args[0] == NULL){
		return false;
	}

	struct inode *inode = NULL;
	struct dir *curr_dir = calloc(1, sizeof *curr_dir);
	if(pathtype){ // absolute path
		curr_dir = dir_open_root();
	}
	else{
		/* very basic relative path */
		cluster_t clst = fat_create_chain(0);
		disk_sector_t inode_sector = cluster_to_sector(clst);
		curr_dir = dir_open_root ();
		bool success = (curr_dir != NULL
				&& clst != 0
				&& inode_create (inode_sector, initial_size, false)
				&& dir_add (curr_dir, args[0], inode_sector));
		if (!success && clst != 0)
			fat_remove_chain(sector_to_cluster(inode_sector), 0);

		dir_close (curr_dir);
		return success;
		/* very basic relative path */
		// ., .., and advanced relative path is left
		// curr_dir = dir_reopen(thread_current()->curr_dir);
	}

	int i;
	for(i = 0; i < 31 && (args[i] != NULL); i++){
		if(args[i + 1] == NULL){
			break;
		}
		if(!dir_lookup(curr_dir, args[i], &inode)){
			dir_close(curr_dir);
			return NULL;
		}
		//printf("condition check fail\n");
		if(!inode_is_dir(inode)){
			dir_close(curr_dir);
			return NULL;
		}

		dir_close(curr_dir);
		curr_dir = dir_open(inode);
	}

	cluster_t clst = fat_create_chain(0);
	disk_sector_t inode_sector = cluster_to_sector(clst);
	bool success = (curr_dir != NULL
			&& clst != 0
			&& inode_create (inode_sector, initial_size, false)
			&& dir_add (curr_dir, args[i], inode_sector));
	if (!success && clst != 0)
		fat_remove_chain(sector_to_cluster(inode_sector), 0);

	dir_close (curr_dir);

	return success;
}

/* Opens the file with the given NAME.
 * Returns the new file if successful or a null pointer
 * otherwise.
 * Fails if no file named NAME exists,
 * or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name) {
	bool pathtype = (name[0] == '/') ? true : false;

	char *tmp = (char *)malloc(strlen(name) + 1);
    strlcpy(tmp, name, strlen(name) + 1);

	char *args[32];
	char *token, *save_ptr;
	int token_cnt = 0;
	token = strtok_r(tmp, "/", &save_ptr);
	while(token != NULL){
		args[token_cnt] = token;
		token = strtok_r(NULL, "/", &save_ptr);
		token_cnt++;
	}
	for(; token_cnt < 32; token_cnt++){
		args[token_cnt] = NULL;
	}
	
	struct inode *inode = NULL;
	struct dir *curr_dir = calloc(1, sizeof *curr_dir);
	if(pathtype){ // absolute path
		curr_dir = dir_open_root();
		if(args[0] == NULL){
			inode = &curr_dir->inode;
			dir_close(curr_dir);
			return file_open(inode);
		}
	}
	else{
		/* very basic relative path */
		struct dir *dir = dir_open_root ();
		if(dir != NULL){
			dir_lookup(dir, name, &inode);
		}
		dir_close(dir);
		return file_open(inode);
		/* very basic relative path */
		// ., .., and advanced relative path is left
		// curr_dir = dir_reopen(thread_current()->curr_dir);
	}
	
	int i;
	for(i = 0; i < 31 && (args[i] != NULL); i++){
		if(args[i + 1] == NULL){
			break;
		}

		if(!dir_lookup(curr_dir, args[i], &inode)){
			dir_close(curr_dir);
			return NULL;
		}
		if(!inode_is_dir(inode)){
			dir_close(curr_dir);
			return NULL;
		}

		dir_close(curr_dir);
		curr_dir = dir_open(inode);
	}

	if(curr_dir != NULL){
		if(!dir_lookup(curr_dir, args[i], &inode)){
			dir_close(curr_dir);
			return NULL;
		}
	}
	dir_close(curr_dir);
	return file_open(inode);
}

/* Deletes the file named NAME.
 * Returns true if successful, false on failure.
 * Fails if no file named NAME exists,
 * or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) {
	struct dir *dir = dir_open_root ();
	bool success = dir != NULL && dir_remove (dir, name);
	dir_close (dir);

	return success;
}

/* Formats the file system. */
static void
do_format (void) {
	printf ("Formatting file system...");

#ifdef EFILESYS
	/* Create FAT and save it to the disk. */
	fat_create ();
	if (!dir_create(cluster_to_sector(ROOT_DIR_CLUSTER), 16))
		PANIC ("root directory creation failed");
	fat_close ();
#else
	free_map_create ();
	if (!dir_create (ROOT_DIR_SECTOR, 16))
		PANIC ("root directory creation failed");
	free_map_close ();
#endif

	printf ("done.\n");
}

bool 
filesys_chdir(const char *dir){
	if(dir == NULL){
		return false;
	}
	char *curr_path = (char *)malloc(strlen(dir) + 1);
	strlcpy(curr_path, dir, strlen(dir) + 1);

	struct dir *curr_dir = calloc(1, sizeof *dir);
	if(curr_path[0] == '/'){ // absolute path
		curr_dir = dir_open_root();
	}
	else{ // relative path, ., .., etc
		free(curr_path); // temp
		return false; // temp
		// curr_dir = dir_reopen(thread_current()->curr_dir);
	}

	char *token, *save_ptr;
	struct inode *inode = NULL;
	token = strtok_r(curr_path, "/", &save_ptr);
	while (token != NULL){
		if(!dir_lookup(curr_dir, token, &inode) || !inode_is_dir(inode)){
			dir_close(curr_dir);
			free(curr_path);
			return false;
		}

		dir_close(curr_dir);
		curr_dir = dir_open(inode);
		if(curr_dir == NULL){
			free(curr_path);
			return false;
		}

		token = strtok_r(NULL, "/", &save_ptr);
	}
	dir_close(thread_current()->curr_dir);
	thread_current()->curr_dir = curr_dir;
	free(curr_path);

	return true;
}

bool
filesys_mkdir(const char *name){
	bool pathtype = (name[0] == '/') ? true : false;

	char *tmp = (char *)malloc(strlen(name) + 1);
    strlcpy(tmp, name, strlen(name) + 1);

	char *args[32];
	char *token, *save_ptr;
	int token_cnt = 0;
	token = strtok_r(tmp, "/", &save_ptr);
	while(token != NULL){
		args[token_cnt] = token;
		token = strtok_r(NULL, "/", &save_ptr);
		token_cnt++;
	}
	for(; token_cnt < 32; token_cnt++){
		args[token_cnt] = NULL;
	}

	if(args[0] == NULL){
		return false;
	}

	struct inode *inode = NULL;
	struct dir *curr_dir = calloc(1, sizeof *curr_dir);
	if(pathtype){ // absolute path
		curr_dir = dir_open_root();
	}
	else{
		/* very basic relative path */
		cluster_t clst = fat_create_chain(0);
		disk_sector_t inode_sector = cluster_to_sector(clst);
		curr_dir = dir_open_root ();
		bool success = (curr_dir != NULL
				&& clst != 0
				&& dir_create (inode_sector, 16)
				&& dir_add (curr_dir, args[0], inode_sector));
		if (!success && clst != 0)
			fat_remove_chain(sector_to_cluster(inode_sector), 0);
		dir_close (curr_dir);
		return success;
		/* very basic relative path */
		// ., .., and advanced relative path is left
		// curr_dir = dir_reopen(thread_current()->curr_dir);
	}

	int i;
	for(i = 0; i < 31 && (args[i] != NULL); i++){
		if(args[i + 1] == NULL){
			break;
		}

		if(!dir_lookup(curr_dir, args[i], &inode)){
			dir_close(curr_dir);
			return NULL;
		}

		if(!inode_is_dir(inode)){
			dir_close(curr_dir);
			return NULL;
		}

		dir_close(curr_dir);
		curr_dir = dir_open(inode);
	}

	cluster_t clst = fat_create_chain(0);
	disk_sector_t inode_sector = cluster_to_sector(clst);
	bool success = (curr_dir != NULL
			&& clst != 0
			&& dir_create (inode_sector, 16)
			&& dir_add (curr_dir, args[i], inode_sector));
	if (!success && clst != 0)
		fat_remove_chain(sector_to_cluster(inode_sector), 0);
	
	dir_close (curr_dir);
	return success;
}