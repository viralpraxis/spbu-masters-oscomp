#include <stdlib.h>

#ifndef HEADERFILE_structs
#define HEADERFILE_structs

#define PATH_MAX 256
#define BLOCK_SIZE 8196

typedef struct
{
	int used;
	char data[BLOCK_SIZE];
	int next_block;
} block;

typedef struct 
{
	int used;
	char path[PATH_MAX];
	int is_dir;
	mode_t mode;
	size_t size;
	int start_block;
} tmpfs_inode;

typedef struct 
{
	size_t block_size;
	size_t blocks_max;
	size_t inodes_max;
	tmpfs_inode* nodes;
	block* blocks;
} storage_t;

#endif