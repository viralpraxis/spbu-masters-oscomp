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

#define MYFS_BLOCKSIZE		4096
#define MYFS_BLOCKSIZE_BITS	12
#define MYFS_MAGIC		0xbeefcafe
#define LOG_LEVEL		KERN_ALERT

#define EOF 0

#define MAX(x, y) (((x) > (y)) ? (x) : (y));
#define MIN(x, y) (((x) > (y)) ? (y) : (x));

/* declarations of functions that are part of operation structures */

DECLARE_HASHTABLE(blocks_data, 8);

struct hnode {
	char *data;
	unsigned long ino;
	int last_block_payload;
	struct hlist_node node;
};

static int myfs_mknod(struct user_namespace *user_ns, struct inode *dir,
		struct dentry *dentry, umode_t mode, dev_t dev);
static int myfs_create(struct user_namespace *user_ns, struct inode *dir, struct dentry *dentry,
		umode_t mode, bool excl);
static int myfs_mkdir(struct user_namespace *user_ns, struct inode *dir, struct dentry *dentry, umode_t mode);

ssize_t myfs_read(struct file *f, char __user *buffer, size_t s, loff_t *offset);
ssize_t myfs_write(struct file*, const char*, size_t, loff_t*);

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

static const struct inode_operations myfs_file_inode_operations = {
	/* TODO 6/1: Fill file inode operations structure. */
	.getattr        = simple_getattr,
};


//static const struct address_space_operations myfs_aops = {
	/* TODO 6/3: Fill address space operations structure. */
//	.readpage       = simple_readpage,
//	.write_begin    = simple_write_begin,
//	.write_end      = simple_write_end,
//};

ssize_t myfs_read(struct file *f, char __user *buffer, size_t len, loff_t *offset) {
	unsigned long to_copy, copy_result;
    unsigned long f_inode = file_inode(f)->i_ino;
    struct hnode *item, *cur_item;
    bool found = false;

	pr_info("MYFS: read(len=%ld, off=%lld) for inode %lu\n", len, *offset, f_inode);

    hash_for_each_possible(blocks_data, cur_item, node, f_inode) {
    	if (f_inode == cur_item->ino) {
          item = cur_item;
          found = true;
          break;
         }
    }

    if (!found) {
    	return EOF;
    }


	if (*offset >= MYFS_BLOCKSIZE) {
        return EOF;
	}

	to_copy = MIN(len, MYFS_BLOCKSIZE - *offset);


    if (item->data == NULL) {
    	memset(buffer, 0, len);
    	*offset += to_copy;

    	return to_copy;
    }

	copy_result = copy_to_user(buffer, item->data + *offset, to_copy);
	if (copy_result > 0) {
		return -EFAULT;
	}

	*offset += to_copy;

	return to_copy;
}

ssize_t myfs_write(struct file* f, const char* buffer, size_t len, loff_t* offset){
  unsigned long to_copy;
  char *kbuffer_tmp, *page;
  int failed_write_count;
  unsigned long f_inode = file_inode(f)->i_ino;
  struct hnode *cur_item, *item;
  bool found = false;

  pr_info("MYFS: write(len=%ld, off=%lld) for inode %lu\n", len, *offset, f_inode);

  hash_for_each_possible(blocks_data, cur_item, node, f_inode) {
  	if (cur_item->ino == f_inode) {
        item = cur_item;
        found = true;
        break;
    }
  }

  if (!found) {
    page = kzalloc(MYFS_BLOCKSIZE, 0);
    item = kzalloc(sizeof(struct hnode), 0);
    item->ino = f_inode;
    item->last_block_payload = 0;
    item->data = page;
    hash_add(blocks_data, &item->node, f_inode);
  } else {
  	page = item->data;
  }

  
  if (*offset >= MYFS_BLOCKSIZE) {
    return EOF;
  };

  to_copy = MIN(len, MYFS_BLOCKSIZE - *offset);


  kbuffer_tmp = kzalloc(to_copy, 0);
  if (kbuffer_tmp == NULL) {
    printk(KERN_ERR "myfs: failed to allocate tmp buffer for write");

    return -ENOMEM;
  }

  // if (first_one_bit == MYFS_BLOCKSIZE) {
  //   item->last_block_payload = 0;
  //   item->data = NULL;

  //   return to_copy;
  // }

  failed_write_count = copy_from_user(kbuffer_tmp, buffer, to_copy);
  if (failed_write_count > 0) {
    printk(KERN_ERR "myfs: Failed to write to tmp buffuer for write");
    kfree(kbuffer_tmp);

    return -EFAULT;
  }

  item->last_block_payload = MAX(item->last_block_payload, *offset + len);

  memcpy(page + *offset, kbuffer_tmp, to_copy);


  int first_one_bit = find_first_bit((unsigned long) page, MYFS_BLOCKSIZE * 8);
  printk("myfs: first one bit: %d\n", first_one_bit);

  if (first_one_bit == MYFS_BLOCKSIZE * 8) {
  	item->data = NULL;
  }

  *offset += to_copy;
  return to_copy;
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

	return 0;
}

static void __exit myfs_exit(void)
{
	struct hnode *cur;
	unsigned bkt;

    hash_for_each(blocks_data, bkt, cur, node) {
      if (cur->data) {
        kfree(cur->data);
       }
    }


	unregister_filesystem(&myfs_fs_type);
}

module_init(myfs_init);
module_exit(myfs_exit);
