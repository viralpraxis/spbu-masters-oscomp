/*
 * SO2 Lab - Filesystem drivers
 * Exercise #1 (no-dev filesystem)
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/hashtable.h>

MODULE_DESCRIPTION("Simple no-dev filesystem");
MODULE_AUTHOR("SO2");
MODULE_LICENSE("GPL");

#define MYFS_BLOCKSIZE		128
#define MYFS_BLOCKSIZE_BITS	12
#define MYFS_MAGIC		0xbeefcafe
#define LOG_LEVEL		KERN_ALERT

#define EOF 0

#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) > (y)) ? (y) : (x))

/* declarations of functions that are part of operation structures */

DECLARE_HASHTABLE(blocks_data, 8);
DEFINE_MUTEX(blocks_data_mu);

struct hnode {
	unsigned long ino;
	char **blocks;
	struct inode *vfs_inode;
	loff_t i_size;

	struct hlist_node node;
};

static int myfs_mknod(struct user_namespace *user_ns, struct inode *dir,
		struct dentry *dentry, umode_t mode, dev_t dev);
static int myfs_create(struct user_namespace *user_ns, struct inode *dir, struct dentry *dentry,
		umode_t mode, bool excl);
static int myfs_mkdir(struct user_namespace *user_ns, struct inode *dir, struct dentry *dentry, umode_t mode);

ssize_t myfs_read(struct file *f, char __user *buffer, size_t s, loff_t *offset);
ssize_t myfs_write(struct file*, const char* __user biffer, size_t, loff_t*);
int myfs_setattr(struct user_namespace *, struct dentry *dentry, struct iattr *iattr);
void acquire_exclusive_hashtable_lock(void);
void release_exclusive_hashtable_lock(void);

/* TODO 2/4: define super_operations structure */
static const struct super_operations myfs_ops = {
	.statfs		= simple_statfs,
	.drop_inode	= generic_drop_inode,
};

static const struct inode_operations myfs_dir_inode_operations = {
	/* TODO 5/8: Fill dir inode operations structure. */
	.create         = myfs_create,
	.lookup         = simple_lookup,
	.link           = simple_link,
	.unlink         = simple_unlink,
	.mkdir          = myfs_mkdir,
	.rmdir          = simple_rmdir,
	.mknod          = myfs_mknod,
	.rename         = simple_rename,
};

static const struct file_operations myfs_file_operations = {
	/* TODO 6/4: Fill file operations structure. */
	//.read_iter      = generic_file_read_iter,
	// .write_iter     = generic_file_write_iter,
	.mmap           = generic_file_mmap,
	.llseek         = generic_file_llseek,
	.read           = myfs_read,
	.write          = myfs_write,
};

void acquire_exclusive_hashtable_lock() {
	mutex_lock(&blocks_data_mu);
}

void release_exclusive_hashtable_lock() {
	mutex_unlock(&blocks_data_mu);
}

static const struct inode_operations myfs_file_inode_operations = {
	/* TODO 6/1: Fill file inode operations structure. */
	.getattr        = simple_getattr,
	.setattr        = myfs_setattr,
};

ssize_t myfs_read(struct file *f, char __user *buffer, size_t len, loff_t *offset) {
	struct inode *vfs_inode = file_inode(f);
	unsigned long f_inode = vfs_inode->i_ino;
	size_t to_copy, left, done;
	int block_index, ret;
	struct hnode *target_node, *node;

	pr_info("MYFS: read(len=%ld, off=%lld) for inode %lu\n", len, *offset, f_inode);

	acquire_exclusive_hashtable_lock();
	hash_for_each_possible(blocks_data, node, node, f_inode) {
		if (f_inode == node->ino) {
				target_node = node;
				break;
				}
	}
	release_exclusive_hashtable_lock();

	if (target_node == NULL) return EOF;
	vfs_inode->i_size = target_node->i_size;
	if (*offset > vfs_inode->i_size) return EOF;

	pr_info("myfs: total bytes: %lld\n", target_node->vfs_inode->i_size);
	to_copy = MIN(len, target_node->vfs_inode->i_size - *offset);

	pr_info("myfs: to_copy: %lu\n", to_copy);

	if (!to_copy) {
		printk("myfs: nothing to read, EOF\n");
		return EOF;
	}

	if (target_node->blocks == NULL) {
		pr_info("myfs: empty read\n");
		return EOF;
	}

  done = 0;
  left = to_copy;
  block_index = (*offset) / MYFS_BLOCKSIZE;

  down_read(&(vfs_inode->i_rwsem));
  if (*offset % MYFS_BLOCKSIZE != 0) {
  	if (!access_ok(buffer + done, MYFS_BLOCKSIZE - (*offset % MYFS_BLOCKSIZE))) {
  		up_read(&(vfs_inode->i_rwsem));
  		return -EINVAL;
  	}
    ret = __copy_to_user(
    	buffer + done, target_node->blocks[block_index] + (*offset % MYFS_BLOCKSIZE),
    	(MYFS_BLOCKSIZE - (*offset % MYFS_BLOCKSIZE))
    );
    left -= (*offset) % MYFS_BLOCKSIZE;
    done += (*offset) % MYFS_BLOCKSIZE;
    printk("myfs: blockind: %d, done: %ld, left: %ld\n", block_index, done, left);
    block_index += 1;
  }

  while (done < to_copy) {
  	if (!access_ok(buffer + done, MIN(left, MYFS_BLOCKSIZE))) {
  		up_read(&(vfs_inode->i_rwsem));
  		return -EINVAL;
  	}

    ret = __copy_to_user(buffer + done, target_node->blocks[block_index], MIN(left, MYFS_BLOCKSIZE));
  	done += MIN(left, MYFS_BLOCKSIZE);
  	left -= MIN(left, MYFS_BLOCKSIZE);

  	printk("myfs: blockind: %d, done: %ld, left: %ld\n", block_index, done, left);
  	block_index += 1;
  }
  up_read(&(vfs_inode->i_rwsem));

  *offset += to_copy;
  return to_copy;
}

ssize_t myfs_write(struct file* f, const char* buffer, size_t len, loff_t* offset){
  struct inode *vfs_inode = file_inode(f);
  unsigned long f_inode = vfs_inode->i_ino;
  struct hnode *target_node, *node;
  int i, ret, required_blocks_count, block_index, last_block_write, initial_blocks_count;
  size_t left, done;
  char **blocks;
  char *page;

  pr_info("MYFS: write(len=%ld, off=%lld) for inode %lu\n", len, *offset, f_inode);

  acquire_exclusive_hashtable_lock();
  hash_for_each_possible(blocks_data, node, node, f_inode) {
  	if (node->ino == f_inode) {
  		target_node = node;
        break;
    }
  }
  if (target_node == NULL) {
  	pr_info("myfs: could not find inode %lu\n", f_inode);
    target_node = kzalloc(sizeof(struct hnode), 0);
    target_node->ino = f_inode;
    target_node->blocks = (char **) kzalloc(sizeof(char *), 0);
    page = kzalloc(MYFS_BLOCKSIZE, 0);
    target_node->blocks[0] = page;
    target_node->vfs_inode = vfs_inode;

    vfs_inode->i_blocks = 1;
    target_node->i_size = 0;
    target_node->vfs_inode->i_size = 0;

    hash_add(blocks_data, &target_node->node, f_inode);
  } else {
  	vfs_inode->i_size = target_node->i_size;
  }
  release_exclusive_hashtable_lock();
  down_write(&(vfs_inode->i_rwsem));

  initial_blocks_count = target_node->vfs_inode->i_blocks;
  required_blocks_count = (*offset + len) / MYFS_BLOCKSIZE;
  if (*offset + len % MYFS_BLOCKSIZE > 0) {
  	required_blocks_count += 1;
  }

  pr_info("myfs: req-blocks-count: %d, total: %llu\n", required_blocks_count, vfs_inode->i_blocks);
  if (required_blocks_count > vfs_inode->i_blocks) {
  	blocks = (char **) kzalloc((required_blocks_count) * sizeof(char *), 0);
  	memcpy(blocks, target_node->blocks, vfs_inode->i_blocks * sizeof(char *));

  	for (i = vfs_inode->i_blocks; i < required_blocks_count; i++) {
  	  blocks[i] = kzalloc(MYFS_BLOCKSIZE, 0);
  	}

  	target_node->blocks = blocks;
  	vfs_inode->i_blocks = required_blocks_count;
  }
  block_index = (*offset) / MYFS_BLOCKSIZE;
  done = 0;
  left = len;
  if (*offset % MYFS_BLOCKSIZE != 0) {
    ret = copy_from_user(target_node->blocks[block_index] + (*offset) % MYFS_BLOCKSIZE, buffer, (MYFS_BLOCKSIZE - ((*offset) % MYFS_BLOCKSIZE)));
    last_block_write = left;
    left -= MYFS_BLOCKSIZE - ((*offset) % MYFS_BLOCKSIZE);
    done += (*offset) % MYFS_BLOCKSIZE;
    block_index += 1;
  }
  while (done < len) {
    ret = copy_from_user(target_node->blocks[block_index], buffer + done, MIN(left, MYFS_BLOCKSIZE));
    last_block_write = left;
  	done += MIN(left, MYFS_BLOCKSIZE);
  	left -= MIN(left, MYFS_BLOCKSIZE);
  	block_index += 1;
  }

  pr_info("myfs: block_index: %d, total: %llu, size: %lld, last: %d\n", block_index, vfs_inode->i_blocks, target_node->i_size, last_block_write);

  if (block_index >= initial_blocks_count ||
  	  ((block_index == initial_blocks_count) && last_block_write > vfs_inode->i_size % MYFS_BLOCKSIZE)) {
  	pr_info("myfs: updates i_size to %llu\n", len + *offset);
    target_node->i_size = len + *offset;
    vfs_inode->i_size = target_node->i_size;
  }
  up_write(&(vfs_inode->i_rwsem));
  *offset += len;
  return len;
}

int setattr(struct dentry *dentry, struct iattr *iattr) {
	return 0;
}


struct inode *myfs_get_inode(struct super_block *sb, const struct inode *dir,
		int mode)
{
	struct inode *inode = new_inode(sb);

	if (!inode)
		return NULL;

	/* TODO 3/3: fill inode structure
	 *     - mode
	 *     - uid
	 *     - gid
	 *     - atime,ctime,mtime
	 *     - ino
	 */
	inode_init_owner(sb->s_user_ns, inode, dir, mode);
	inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);
	inode->i_ino = 1;

	/* TODO 5/1: Init i_ino using get_next_ino */
	inode->i_ino = get_next_ino();

	/* TODO 6/1: Initialize address space operations. */
	inode->i_mapping->a_ops = &ram_aops;

	if (S_ISDIR(mode)) {
		/* TODO 3/2: set inode operations for dir inodes. */
		inode->i_op = &simple_dir_inode_operations;
		inode->i_fop = &simple_dir_operations;

		/* TODO 5/1: use myfs_dir_inode_operations for inode
		 * operations (i_op).
		 */
		inode->i_op = &myfs_dir_inode_operations;

		/* TODO 3/1: directory inodes start off with i_nlink == 2 (for "." entry).
		 * Directory link count should be incremented (use inc_nlink).
		 */
		inc_nlink(inode);
	}

	/* TODO 6/4: Set file inode and file operations for regular files
	 * (use the S_ISREG macro).
	 */
	if (S_ISREG(mode)) {
		inode->i_op = &myfs_file_inode_operations;
		inode->i_fop = &myfs_file_operations;
	}

	return inode;
}

/* TODO 5/33: Implement myfs_mknod, myfs_create, myfs_mkdir. */
static int myfs_mknod(struct user_namespace *user_ns, struct inode *dir,
		struct dentry *dentry, umode_t mode, dev_t dev)
{
	struct inode *inode = myfs_get_inode(dir->i_sb, dir, mode);

	if (inode == NULL)
		return -ENOSPC;

	d_instantiate(dentry, inode);
	dget(dentry);
	dir->i_mtime = dir->i_ctime = current_time(inode);

	return 0;
}

int myfs_setattr(struct user_namespace *user_ns, struct dentry *dentry, struct iattr *attr) {
    struct inode *inode = d_inode(dentry);
    int error;

	pr_info("myfs: setattr\n");

	error = setattr_prepare(user_ns, dentry, attr);
	if (error)
					return error;

	if ((attr->ia_valid & ATTR_SIZE) &&
		attr->ia_size != i_size_read(inode)) {
			error = inode_newsize_ok(inode, attr->ia_size);
			if (error) return error;

			truncate_setsize(inode, attr->ia_size);
			pr_info("myfs: truncate: newsize: %lld\n", attr->ia_size);
			//todo impl
  }

	setattr_copy(user_ns, inode, attr);
	mark_inode_dirty(inode);

	return 0;
}

static int myfs_create(struct user_namespace *user_ns, struct inode *dir, struct dentry *dentry,
		umode_t mode, bool excl)
{
	return myfs_mknod(user_ns, dir, dentry, mode | S_IFREG, 0);
}

static int myfs_mkdir(struct user_namespace *user_ns, struct inode *dir, struct dentry *dentry, umode_t mode)
{
	int ret;

	ret = myfs_mknod(user_ns, dir, dentry, mode | S_IFDIR, 0);
	if (ret != 0)
		return ret;

	inc_nlink(dir);

	return 0;
}

static int myfs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct inode *root_inode;
	struct dentry *root_dentry;

	/* TODO 2/5: fill super_block
	 *   - blocksize, blocksize_bits
	 *   - magic
	 *   - super operations
	 *   - maxbytes
	 */
	sb->s_maxbytes = MAX_LFS_FILESIZE;
	sb->s_blocksize = MYFS_BLOCKSIZE;
	sb->s_blocksize_bits = MYFS_BLOCKSIZE_BITS;
	sb->s_magic = MYFS_MAGIC;
	sb->s_op = &myfs_ops;

	/* mode = directory & access rights (755) */
	root_inode = myfs_get_inode(sb, NULL,
			S_IFDIR | S_IRWXU | S_IRGRP |
			S_IXGRP | S_IROTH | S_IXOTH);

	printk(LOG_LEVEL "root inode has %d link(s)\n", root_inode->i_nlink);

	if (!root_inode)
		return -ENOMEM;

	root_dentry = d_make_root(root_inode);
	if (!root_dentry)
		goto out_no_root;
	sb->s_root = root_dentry;

	return 0;

out_no_root:
	iput(root_inode);
	return -ENOMEM;
}

static struct dentry *myfs_mount(struct file_system_type *fs_type,
		int flags, const char *dev_name, void *data)
{
	/* TODO 1/1: call superblock mount function */
	return mount_nodev(fs_type, flags, data, myfs_fill_super);
}

/* TODO 1/6: define file_system_type structure */
static struct file_system_type myfs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "myfs",
	.mount		= myfs_mount,
	.kill_sb	= kill_litter_super,
};

static int __init myfs_init(void)
{
	int err;

	/* TODO 1/1: register */
	err = register_filesystem(&myfs_fs_type);
	if (err) {
		printk(LOG_LEVEL "myfs: register_filesystem failed\n");

		return err;
	}

	hash_init(blocks_data);
	mutex_init(&blocks_data_mu);

	return 0;
}

static void __exit myfs_exit(void)
{
	struct hnode *cur;
	unsigned bkt;
	int i;

    pr_info("myfs: destruct: going to acquire excl lock\n");
	acquire_exclusive_hashtable_lock();
    pr_info("myfs: destruct: excl lock OK\n");
	hash_for_each(blocks_data, bkt, cur, node) {
		if (cur->blocks) {
			for (i = 0; i < cur->vfs_inode->i_blocks; i++) {
				kfree(cur->blocks[i]);
			}

			kfree(cur->blocks);
			}
	}
	release_exclusive_hashtable_lock();

	unregister_filesystem(&myfs_fs_type);
}

module_init(myfs_init);
module_exit(myfs_exit);
