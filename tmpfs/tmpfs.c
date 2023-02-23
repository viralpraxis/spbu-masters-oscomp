#define FUSE_USE_VERSION 26

#include <stdlib.h>
#include <fuse.h>
#include <unistd.h>
#include <string.h>
#include <linux/limits.h>
#include <errno.h>
#include <sys/stat.h>


struct tmpfs_state {
  char *rootdir;
};

#define TMPFS_DATA ((struct tmpfs_state *) fuse_get_context()->private_data)

// https://www.gnu.org/software/libc/manual/html_node/Attribute-Meanings.html
static int do_getattr(const char *path, struct stat *st )
{
  st->st_uid = getuid();
  st->st_gid = getgid();
  st->st_atime = time(NULL);
  st->st_mtime = time(NULL);

  if (strcmp( path, "/") == 0) {
    st->st_mode = S_IFDIR | 0755;
    st->st_nlink = 2;
  } else {
    st->st_mode = S_IFREG | 0644;
    st->st_nlink = 1;
    st->st_size = 1024;
  }

  return EXIT_SUCCESS;
}

static int do_readdir( const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi ) {
  filler( buffer, ".", NULL, 0 );
  filler( buffer, "..", NULL, 0 );

  filler(buffer, "cow", NULL, 0);

  return EXIT_SUCCESS;
}

static int do_read( const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi ) {
  char* content = "moo\n";

  memcpy(buffer, content, (size_t) sizeof(content));

  return strlen(content);
}

// TODO: Provide implementation.
static int do_mkdir( const char *path, mode_t mode) {
  return EXIT_SUCCESS;
}

// TODO: Provide implementation.
static int do_mknod (const char *path, mode_t mode, dev_t rdev) {
  return EXIT_SUCCESS;
}

// TODO: Provide implementation.
static int do_opendir (const char *, struct fuse_file_info *) {
  return EXIT_SUCCESS;
}

// TODO: Provide implementation.
static int do_write( const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *info ) {
  return EXIT_SUCCESS;
}

static struct fuse_operations operations = {
    .getattr  = do_getattr,
    .mknod    = do_mknod,
    .mkdir    = do_mkdir,
    .read     = do_read,
    .write    = do_write,
    .opendir  = do_opendir,
    .readdir  = do_readdir,
};

int main(int argc, char *argv[]) {
  return fuse_main(argc, argv, &operations, NULL);
}
