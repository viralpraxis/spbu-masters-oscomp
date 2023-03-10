#define FUSE_USE_VERSION 26

#include "tmpfs.h"
#include "structs.h"
#include "utils.h"

storage_t storage;

int tmpfs_init() {
	storage.block_size = BLOCK_SIZE;
	storage.nodex_max = 1024;
	storage.blocks_max = 8096;
	
	storage.nodes = (tmpfs_inode*) malloc(storage.nodex_max * sizeof(tmpfs_inode));
	storage.blocks = (block*) malloc(storage.blocks_max * sizeof(block));
	
	nodes_init(storage);
	blocks_init(storage);
	
	return EXIT_SUCCESS;
}

void tmpfs_destroy(void *v) {
	free(storage.nodes);
	free(storage.blocks);
}

int tmpfs_getattr(const char *path, struct stat *stbuf)
{
	if (strcmp(path, "/") == 0) {
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
		stbuf->st_size = storage.block_size;
		return EXIT_SUCCESS;
	} 
	
	int i = find_node_index(storage, path);
	if (i < 0) return i;

	stbuf->st_nlink = 1;
	stbuf->st_mode = storage.nodes[i].mode;
	stbuf->st_size = storage.nodes[i].size;	

	return EXIT_SUCCESS;
}

int tmpfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
	int i, j = -1;
	char *dirpath = "";
	
	if (strcmp(path, "/") != 0) {
		for (i = 0; i < storage.nodex_max; i++) {
			if (storage.nodes[i].is_dir == 1 && storage.nodes[i].used == 1 && strcmp(path, storage.nodes[i].path) == 0) {
				j = i;
				dirpath = storage.nodes[i].path;
				break;
			}
		}

		if (j == -1) return -ENOENT;
	
	}
	
	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);

	for (i = 0; i < storage.nodex_max; i++) {
		if (storage.nodes[i].used == 1 && subdir(dirpath, storage.nodes[i].path) == 0) {
			filler(buf, strrchr(storage.nodes[i].path, '/')+1 , NULL, 0);
		}
	}

	return EXIT_SUCCESS;
}

int tmpfs_open(const char *path, struct fuse_file_info *fi) {
	if (find_node_index(storage, path) == -ENOENT) return -ENOENT;	

	return EXIT_SUCCESS;
}

int tmpfs_create(const char *path, mode_t mode, struct fuse_file_info *fi) 
{
	if (find_node_index(storage, path) == -ENOENT) {
		tmpfs_inode* file = get_free_node(storage);
		file->mode = mode;
		strcpy(file->path, path);	
	}
	
	return EXIT_SUCCESS;
}

int tmpfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
	int fileblock, sizetmp, i;
	i = find_node_index(storage, path);
	if (i < 0) return i;
	if (size > storage.nodes[i].size) size = storage.nodes[i].size;
	sizetmp = storage.block_size;
	fileblock = storage.nodes[i].start_block;
	if (fileblock == -1) return EXIT_SUCCESS;
	
	while (offset-sizetmp > 0) {
		fileblock = storage.blocks[fileblock].next_block;
		offset -= sizetmp;
	}
	
	if (offset > 0) {
		memcpy(buf, storage.blocks[fileblock].data + offset , sizetmp-offset);
		fileblock = storage.blocks[fileblock].next_block;
	}
	
	while (offset < size) {	
		if (offset + sizetmp > size){
			sizetmp = size - offset;
		}

		if (fileblock == -1) break;
		
		memcpy(buf + offset, storage.blocks[fileblock].data , sizetmp);
		fileblock = storage.blocks[fileblock].next_block;
		offset += sizetmp;
	}
	
	return size;
}

int tmpfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
	int fileblock, sizetmp, i;
	i = find_node_index(storage, path);
	if (i < 0) return i;
			
	storage.nodes[i].size = offset;
	sizetmp = storage.block_size;
	fileblock = storage.nodes[i].start_block;

	if (fileblock == -1 && size > 0) {
		fileblock = get_free_block_index(storage);
		storage.nodes[i].start_block = fileblock;
	}

	while (offset - sizetmp > 0) {
		fileblock = storage.blocks[fileblock].next_block;
		offset -= sizetmp;
	}

	if (offset > 0) {
		memcpy(storage.blocks[fileblock].data + offset, buf, sizetmp-offset);
		fileblock = storage.blocks[fileblock].next_block;
		offset = sizetmp - offset;
	}

	while (offset < size) {	
		if (offset + sizetmp > size) sizetmp = size - offset;

		memcpy(storage.blocks[fileblock].data, buf + offset , sizetmp);
		fileblock = storage.blocks[fileblock].next_block;
		offset += sizetmp;
	}
	
	storage.nodes[i].size += offset;
	
	return offset;	 
}

int tmpfs_mkdir(const char *path, mode_t mode)
{
	if (find_node_index(storage, path) == -ENOENT) {
		tmpfs_inode* dir = get_free_node(storage);
		
		dir->is_dir = 1;
		dir->mode = S_IFDIR|mode;
		dir->size = storage.block_size;
		strcpy(dir->path, path);

		return EXIT_SUCCESS;
	}
	
	return -EEXIST;
}


int tmpfs_truncate(const char *path, off_t offset)
{
	return EXIT_SUCCESS;	
}

int tmpfs_opendir(const char *path, struct fuse_file_info *fu)
{
	if (strcmp("/", path) == 0) {
		return EXIT_SUCCESS;
	}

	for (int i = 0; i < storage.nodex_max; i++) {
		if (storage.nodes[i].is_dir == 1 && storage.nodes[i].used == 1 && strcmp(path, storage.nodes[i].path) == 0) {
			return EXIT_SUCCESS;
		}
	}

	return -ENOENT;
}

int tmpfs_utimens(const char *, const struct timespec tv[2]) {
  return EXIT_SUCCESS; // TODO
}

struct fuse_operations operations = {
	.getattr	 = tmpfs_getattr,
	.readdir	 = tmpfs_readdir,
	.opendir	 = tmpfs_opendir,
	.mkdir		 = tmpfs_mkdir,
	.create		 = tmpfs_create,
	.truncate    = tmpfs_truncate,
	.open		 = tmpfs_open,
	.read		 = tmpfs_read,
	.write 		 = tmpfs_write,
	.destroy 	 = tmpfs_destroy,
    .utimens     = tmpfs_utimens,
};

int main(int argc, char *argv[])
{
	tmpfs_init();

	return fuse_main(argc, argv, &operations, NULL);
}