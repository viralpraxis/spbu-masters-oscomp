#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <fnmatch.h>
#include <fuse.h>

#ifndef HEADERFILE_tmpfs
#define HEADERFILE_tmpfs

int tmpfs_init();
void tmpfs_destroy(void *v);

int tmpfs_getattr(const char *path, struct stat *stbuf);
int tmpfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi);
int tmpfs_create(const char *path, mode_t mode, struct fuse_file_info *fi);
int tmpfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
int tmpfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
int tmpfs_mkdir(const char *path, mode_t mode);
int tmpfs_truncate(const char *path, off_t offset);
int tmpfs_opendir(const char *path, struct fuse_file_info *fu);
int tmpfs_flush(const char *, struct fuse_file_info *);
int tmpfs_utimens(const char *, const struct timespec tv[2]);
int tmpfs_rmdir(const char *path);
int tmpfs_unlink(const char *path);
int tmpfs_rename(const char *path, const char *destpath);
int tmpfs_chmod(const char *, mode_t);

#endif
