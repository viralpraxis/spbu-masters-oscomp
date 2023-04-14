#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>

#define DEVNAME "membuf"
#define DEVICES_COUNT 16
#define EXIT_SUCCESS 0
#define EOF 0

#define MAX(x, y) (((x) > (y)) ? (x) : (y));
#define MIN(x, y) (((x) < (y)) ? (x) : (y));

MODULE_LICENSE("GPL");
MODULE_AUTHOR("yk");
MODULE_DESCRIPTION("Membuf kernel module");
MODULE_VERSION("1.0.0");

static int buffer_size = 256;

static ssize_t dev_read(struct file*, char*, size_t, loff_t*);
static ssize_t dev_write(struct file*, const char*, size_t, loff_t*);

static struct file_operations operations = {
  .owner = THIS_MODULE,
  .read = dev_read,
  .write = dev_write,
};

static struct cdev *cdev_array;
static struct mutex *mutex_array;
static struct class *my_class;

dev_t dev = 0;
static char **kbuffer;

static void acquire_exclusive_lock(void) {
  for (int i = 0; i < DEVICES_COUNT; i++) {
    mutex_lock(&mutex_array[i]);
  }
}

static void release_exclusive_lock(void) {
	for (int i = 0; i < DEVICES_COUNT; i++) {
		mutex_unlock(&mutex_array[i]);
  }
}

static int buffer_size_setter(const char *value, const struct kernel_param *kparam) {
  char **buffer_tmp;
	int initial_size = buffer_size;
	int retval;
  int i;

  printk(KERN_INFO "membuf: parameter 'buffer_size' is about to be updated\n");

	acquire_exclusive_lock();

  retval = param_set_uint(value, kparam);
	if (retval < 0) {
    printk(KERN_ERR "membuf: failed to update parameter 'buffer_size\n");
		release_exclusive_lock();
		return retval;
	}

  buffer_tmp = kmalloc(DEVICES_COUNT * sizeof(char *), 0);
  if (buffer_tmp == NULL) {
    printk(KERN_ERR "membuf: failed to allocate memory for buffer\n");
    release_exclusive_lock();
    return -ENOMEM;
  }
  for (i = 0; i < DEVICES_COUNT; i++) {
    buffer_tmp[i] = kzalloc(buffer_size, 0);
    if (buffer_tmp[i] == NULL) {
      printk(KERN_ERR "membuf: failed to allocate memory for buffer\n");
      while (i > 0) {
        kfree(buffer_tmp[i]);
      }
      kfree(buffer_tmp);
      release_exclusive_lock();
      return -ENOMEM;
    }
  }

  initial_size = MIN(initial_size, buffer_size);
  for (i = 0; i < DEVICES_COUNT; i++) {
    memcpy(buffer_tmp[i], kbuffer[i], initial_size);
    kfree(kbuffer[i]);
  }

  kfree(kbuffer);
  kbuffer = buffer_tmp;
	release_exclusive_lock();

	return EXIT_SUCCESS;
}

static const struct kernel_param_ops kparam_buffer_size_ops = {
	.set = buffer_size_setter,
	.get = param_get_uint,
};

module_param_cb(buffer_size, &kparam_buffer_size_ops, &buffer_size, S_IWUSR | S_IRUSR); // Write and read permissions
MODULE_PARM_DESC(buffer_size, "Size of buffer for each device");

static ssize_t dev_write(struct file *f, const char *buffer, size_t
len, loff_t* offset) {
  unsigned long to_copy;
  char* kbuffer_tmp;
  int failed_write_count;
  int minor = iminor(file_inode(f));

  pr_info("membuf: write(len=%ld, off=%lld) for device %d\n", len, *offset, minor);

  mutex_lock(&mutex_array[minor]);

  if (*offset >= buffer_size) {
    mutex_unlock(&mutex_array[minor]);
    return EOF;
  };

  if (*offset + len > buffer_size) {
    to_copy = buffer_size - *offset;
  } else {
    to_copy = len;
  }

  kbuffer_tmp = kzalloc(to_copy, 0);
  if (kbuffer_tmp == NULL) {
    printk(KERN_ERR "membuf: failed to allocate tmp buffer for write");
    mutex_unlock(&mutex_array[minor]);

    return -ENOMEM;
  }

  failed_write_count = copy_from_user(kbuffer_tmp, buffer, to_copy);
  if (failed_write_count > 0) { // if we failed to copy entire buffer we prefer to abort entire operation
    printk(KERN_ERR "Failed to write to tmp buffuer for write");
    kfree(kbuffer_tmp);
    mutex_unlock(&mutex_array[minor]);

    return -EFAULT;
  }

  memcpy(kbuffer[minor] + *offset, kbuffer_tmp, to_copy);
  mutex_unlock(&mutex_array[minor]);

  *offset += to_copy;
  return to_copy;
}

static ssize_t dev_read(struct file *filep, char *buffer, size_t len, loff_t *offset) {
  unsigned long copy_result;
  unsigned long to_copy;
  int minor = iminor(file_inode(filep));
  mutex_lock(&mutex_array[minor]);

  pr_info("MEMBUF: read(len=%ld, off=%lld) for device %d\n", len, *offset, minor);

  if (*offset >= buffer_size) {
    mutex_unlock(&mutex_array[minor]);
    return EOF;
  }

  to_copy = MIN(len, buffer_size);
  copy_result = copy_to_user(buffer, kbuffer[minor] + *offset, to_copy);
  if (copy_result > 0) {
    mutex_unlock(&mutex_array[minor]);
    return -EFAULT;
  }

  mutex_unlock(&mutex_array[minor]);
  *offset += to_copy;

  return to_copy;
}

static void dealocate_kbuffer(void) {
  int i;

  if (kbuffer != NULL) {
    for (i = 0; i < DEVICES_COUNT; i++) {
      if (kbuffer[i] != NULL) {
        kfree(kbuffer[i]);
      }
    }
    kfree(kbuffer);
  }
}

static int __init kmodule_membuf_init(void) {
  int res;
  int i;
  int retval = -1;
  int major;
  dev_t my_device;

  cdev_array = kmalloc(DEVICES_COUNT * sizeof(struct cdev), 0);
  mutex_array = kmalloc(DEVICES_COUNT * sizeof(struct mutex), 0);

  kbuffer = (char **) kzalloc(DEVICES_COUNT * sizeof(char *), 0);
  if (kbuffer == NULL) {
    printk(KERN_ERR "membuf: failed to allocate memory\n");
    return -1;
  }

  for (i = 0; i < DEVICES_COUNT; i++) {
    kbuffer[i] = kzalloc(buffer_size, 0);
    if (kbuffer[i] == NULL) {
      printk(KERN_ERR "membuf: failed to allocate memory for buffer\n");
      while (i > 0) {
        kfree(kbuffer[i--]);
      }
      kfree(kbuffer);

      return -EFAULT;
    }
  }

  for (i = 0; i < DEVICES_COUNT; i++) {
    mutex_init(&mutex_array[i]);
  }

  if ((res = alloc_chrdev_region(&dev, 0, DEVICES_COUNT, DEVNAME)) < 0) {
    printk(KERN_ERR "membuf: failed to allocate major device number\n");
    dealocate_kbuffer();
    return res;
  }

  pr_info("membuf: loaded, Major = %d Minor = %d\n", MAJOR(dev), MINOR(dev));

  major = MAJOR(dev);

  my_class = class_create(THIS_MODULE, "membuf_class");
  for (int i = 0; i < DEVICES_COUNT; i++) {
    my_device = MKDEV(major, i);
    cdev_init(&cdev_array[i], &operations);
    retval = cdev_add(&cdev_array[i], my_device, 1);
    if (retval < 0) {
      pr_err("membuf: cdev_init failed\n");
      dealocate_kbuffer();
      return res;
    } else {
      device_create(my_class, NULL, my_device, NULL, "membuf%d", i);
    }
  }

  return EXIT_SUCCESS;
}

static void __exit kmodule_membuf_exit(void) {
  int major = MAJOR(dev);
  dev_t my_device;

  for (int i = 0; i < DEVICES_COUNT; i++) {
    my_device = MKDEV(major, i);
    cdev_del(&cdev_array[i]);
    device_destroy(my_class, my_device);
  }
  class_destroy(my_class);
  unregister_chrdev_region(dev, DEVICES_COUNT);
  dealocate_kbuffer();

  printk(KERN_INFO "membuf: Unloaded module\n");
}


module_init(kmodule_membuf_init);
module_exit(kmodule_membuf_exit);
