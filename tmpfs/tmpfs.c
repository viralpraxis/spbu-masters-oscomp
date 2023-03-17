#define FUSE_USE_VERSION 26

#include "tmpfs.h"
#include "structs.h"
#include "utils.h"

storage_t storage;

int tmpfs_init() {
	storage.block_size = BLOCK_SIZE;
	storage.inodes_max = 1024;
	storage.blocks_max = 8096;

	storage.nodes = (tmpfs_inode*) malloc(storage.inodes_max * sizeof(tmpfs_inode));
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
		for (i = 0; i < storage.inodes_max; i++) {
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

	for (i = 0; i < storage.inodes_max; i++) {
		if (storage.nodes[i].used == 1 && subdir(dirpath, storage.nodes[i].path) == 0) {
			filler(buf, strrchr(storage.nodes[i].path, '/')+1 , NULL, 0);
		}
	}

	return EXIT_SUCCESS;
}

int tmpfs_open(const char *path, struct fuse_file_info *fi) {
	int node_index = find_node_index(storage, path);
	if (node_index == -ENOENT) return -ENOENT;

	fi->fh = node_index;

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
	int next_block, sizetmp, i;
	i = find_node_index(storage, path);
	if (i < 0) return i;
	if (size > storage.nodes[i].size) size = storage.nodes[i].size;
	sizetmp = storage.block_size;
	next_block = storage.nodes[i].start_block;
	if (next_block == -1) return EXIT_SUCCESS;

	while (offset-sizetmp > 0) {
		next_block = storage.blocks[next_block].next_block;
		offset -= sizetmp;
	}

	if (offset > 0) {
		memcpy(buf, storage.blocks[next_block].data + offset , sizetmp-offset);
		next_block = storage.blocks[next_block].next_block;
	}

	while (offset < size) {
		if (offset + sizetmp > size){
			sizetmp = size - offset;
		}

		if (next_block == -1) break;

		memcpy(buf + offset, storage.blocks[next_block].data , sizetmp);
		next_block = storage.blocks[next_block].next_block;
		offset += sizetmp;
	}

	return size;
}

int tmpfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
	int i = find_node_index(storage, path);
	int last = 0;
	if (i < 0) return i;

	storage.nodes[i].size = offset;
	int blocksize = storage.block_size;
	int next_block = storage.nodes[i].start_block;

	if (next_block == -1 && size > 0) {
		next_block = get_free_block_index(storage);
		if (next_block == -1) {
			storage.nodes[i].size = 0;
			return -EFBIG;
		}
		storage.nodes[i].start_block = next_block;
	}

	while (offset - blocksize > 0) {
		next_block = storage.blocks[next_block].next_block;
		offset -= blocksize;
	}

	if (offset > 0) {
		memcpy(storage.blocks[next_block].data + offset, buf, blocksize-offset);
		last = next_block;
		next_block = storage.blocks[next_block].next_block;
		offset = blocksize - offset;
	}

	while (offset < size) {
		if (offset + blocksize > size) {
			blocksize = size - offset;
		}

		if (next_block == -1) {
			next_block = get_free_block_index(storage);
			if (next_block == -1) return -EFBIG;
			storage.blocks[last].next_block = next_block;
		}

		memcpy(storage.blocks[next_block].data, buf + offset , blocksize);
		last = next_block;
		next_block = storage.blocks[next_block].next_block;
		offset += blocksize;
	}

	storage.nodes[i].size += offset;

	if (offset > size) {
		return size;
	} else {
		return offset;
	}
}

int tmpfs_mkdir(const char *path, mode_t mode)
{
	if (find_node_index(storage, path) == -ENOENT) {
		tmpfs_inode* dir = get_free_node(storage);

		dir->is_dir = 1;
		dir->mode = S_IFDIR | mode;
		dir->size = storage.block_size;
		strcpy(dir->path, path);

		return EXIT_SUCCESS;
	}

	return -EEXIST;
}

int tmpfs_truncate(const char *path, off_t offset)
{
	int i = find_node_index(storage, path);
	if (i < 0) return i;

	int blocksize = storage.block_size;
	int next_block = storage.nodes[i].start_block;

	while (offset-blocksize > 0) {
		next_block = storage.blocks[next_block].next_block;
		offset -= blocksize;
	}

	if (offset > 0) {
		memset(storage.blocks[next_block].data + offset, '\0', blocksize-offset);
		next_block = storage.blocks[next_block].next_block;
		offset = blocksize - offset;
	}

	while (next_block != -1) {
		next_block = free_block(storage, next_block);
	}

	if (offset == 0) storage.nodes[i].start_block = BLOCK_INDEX_DEFAULT;

	storage.nodes[i].size = offset;

	return EXIT_SUCCESS;
}

int tmpfs_flush(const char *, struct fuse_file_info *) {
	// we don't have to do anything here, there are no caches/buffers
	return EXIT_SUCCESS;
}

int tmpfs_opendir(const char *path, struct fuse_file_info *fu)
{
	if (strcmp("/", path) == 0) {
		return EXIT_SUCCESS;
	}

	for (int i = 0; i < storage.inodes_max; i++) {
		if (storage.nodes[i].is_dir == 1 && storage.nodes[i].used == 1 && strcmp(path, storage.nodes[i].path) == 0) {
			return EXIT_SUCCESS;
		}
	}

	return -ENOENT;
}

int tmpfs_utimens(const char *, const struct timespec tv[2]) {
  return EXIT_SUCCESS;
}

int tmpfs_rmdir(const char *path)
{
	int index = -1;
	int i = 0;

	if (strcmp(path, "/") == 0) return -EACCES; // TODO: Allow root deletion?
	for (i = 0; i < storage.inodes_max; i++) {
		if (storage.nodes[i].is_dir == 1 && storage.nodes[i].used == 1 && strcmp(path, storage.nodes[i].path) == 0) {
		    index = i;
			break;
		}
	}

	if (index == -1) return -ENOENT;

	for (int j = 0; j < storage.inodes_max; j++) {
		if (storage.nodes[j].used == 1 && storage.nodes[j].is_dir == 0 && subdir(path, storage.nodes[j].path) == 0) {
			return -ENOTEMPTY;
		}
	}

	storage.nodes[i].used = 0;

	return EXIT_SUCCESS;
}

int tmpfs_unlink(const char *path)
{
	int status = tmpfs_truncate(path, 0);
	if (status < 0) return status;

	int i = find_node_index(storage, path);
	storage.nodes[i].start_block = BLOCK_INDEX_DEFAULT;
	storage.nodes[i].used = 0;

	return EXIT_SUCCESS;
}

int tmpfs_rename(const char *path, const char *destpath)
{
	if (is_file_prefix(path, destpath)) {
		return -EINVAL;
	}
	int i = find_node_index(storage, path);
	if (i < 0) return i;

	int j = find_node_index(storage, destpath);

  if (j >= 0 && storage.nodes[j].is_dir == 1 && storage.nodes[i].is_dir == 0) {
		return -EISDIR;
	}

	if (j >= 0 && storage.nodes[j].is_dir == 0) tmpfs_unlink(destpath);
	strcpy(storage.nodes[i].path, destpath);

	return EXIT_SUCCESS;
}

int tmpfs_chmod(const char *path, mode_t) {
	int i = find_node_index(storage, path);
	if (i < 0) return i;

	return EXIT_SUCCESS;
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
	.flush       = tmpfs_flush,
	.rmdir       = tmpfs_rmdir,
	.unlink      = tmpfs_unlink,
	.rename      = tmpfs_rename,
	.chmod       = tmpfs_chmod,
};

int main(int argc, char *argv[])
{
	tmpfs_init();

	return fuse_main(argc, argv, &operations, NULL);
}
