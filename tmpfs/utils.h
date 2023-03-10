#include "structs.h"

#include <fnmatch.h>
#include <string.h>
#include <errno.h>

#ifndef HEADERFILE_utils
#define HEADERFILE_utils

#define BLOCK_INDEX_DEFAULT -1

void nodes_init(storage_t storage);
void blocks_init(storage_t storage);
tmpfs_inode* get_free_node(storage_t storage);
int get_free_block_index(storage_t storage);
int subdir(const char *dir, const char *path);
int find_node_index(storage_t storage, const char *path);
int free_block(storage_t storage, int index);

#endif