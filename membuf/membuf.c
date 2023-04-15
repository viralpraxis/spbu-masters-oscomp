#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>

#define DEVNAME "membuf"
#define DEVICES_COUNT 16
#define INITIAL_BUFFER_SIZE 256
#define MAX_BUFFER_SIZE 1024
#define EXIT_SUCCESS 0
#define EOF 0

#define MAX(x, y) (((x) > (y)) ? (x) : (y));
#define MIN(x, y) (((x) < (y)) ? (x) : (y));

MODULE_LICENSE("GPL");
MODULE_AUTHOR("yk");
MODULE_DESCRIPTION("Membuf kernel module");
MODULE_VERSION("1.0.0");

static bool kbuffer_allocated = false;
static bool chrdev_region_allocated = false;
static bool class_created = false;

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
static uint buffer_size_data[DEVICES_COUNT];
static char **kbuffer;

static int buffer_size_getter(char *buffer, const struct kernel_param *kp) {
  memcpy(buffer, &buffer_size_data, DEVICES_COUNT * sizeof(uint));

  return DEVICES_COUNT * sizeof(uint);
}

static int buffer_size_setter(const char *raw_value, const struct kernel_param *kparam) {
  uint device_index;
  uint value;
  uint initial_value;
  char *tmp_buffer;

  memcpy(&device_index, raw_value, sizeof(uint));
  memcpy(&value, (uint *) raw_value + 1, sizeof(uint));

  if (device_index > DEVICES_COUNT) {
    printk(KERN_ERR "membuf: invalid parameter 'buffer_size_data' value");

    return -1;
  }
  initial_value = buffer_size_data[device_index];

  value = MIN(value, MAX_BUFFER_SIZE);

  mutex_lock(&mutex_array[device_index]);

  buffer_size_data[device_index] = value;

  tmp_buffer = kzalloc(value, 0);
  memcpy(tmp_buffer, kbuffer[device_index], initial_value);
  kfree(kbuffer[device_index]);
  kbuffer[device_index] = tmp_buffer;

  pr_info("membuf: param 'buffer_size_data' updated (devminor=%d, value=%d)\n", device_index, value);

  mutex_unlock(&mutex_array[device_index]);

  return EXIT_SUCCESS;
}

static const struct kernel_param_ops kparam_buffer_size_ops = {
  .set = buffer_size_setter,
  .get = buffer_size_getter,
};

module_param_cb(buffer_size_data, &kparam_buffer_size_ops, &buffer_size_data, S_IWUSR | S_IRUSR);
MODULE_PARM_DESC(buffer_size_data, "Per-device buffer size");

static ssize_t dev_write(struct file *f, const char *buffer, size_t
len, loff_t* offset) {
  unsigned long to_copy;
  char* kbuffer_tmp;
  int failed_write_count;
  int minor = iminor(file_inode(f));

  pr_info("membuf: write(len=%ld, off=%lld) for device %d\n", len, *offset, minor);

  mutex_lock(&mutex_array[minor]);

  if (*offset >= buffer_size_data[minor]) {
    mutex_unlock(&mutex_array[minor]);
    return EOF;
  };

  if (*offset + len > buffer_size_data[minor]) {
    to_copy = buffer_size_data[minor] - *offset;
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

  if (*offset >= buffer_size_data[minor]) {
    mutex_unlock(&mutex_array[minor]);
    return EOF;
  }

  to_copy = MIN(len, buffer_size_data[minor]);
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

  for (i = 0; i < DEVICES_COUNT; i++) {
    buffer_size_data[i] = INITIAL_BUFFER_SIZE;
  }

  kbuffer = (char **) kzalloc(DEVICES_COUNT * sizeof(char *), 0);
  if (kbuffer == NULL) {
    goto module_cleanup;
    printk(KERN_ERR "membuf: failed to allocate memory\n");
    return -1;
  }

  for (i = 0; i < DEVICES_COUNT; i++) {
    kbuffer[i] = kzalloc(buffer_size_data[i], 0);
    if (kbuffer[i] == NULL) {
      printk(KERN_ERR "membuf: failed to allocate memory for buffer\n");
      while (i > 0) {
        kfree(kbuffer[i--]);
      }
      kfree(kbuffer);

      goto module_cleanup;
      return -EFAULT;
    }
  }
  kbuffer_allocated = true;

  for (i = 0; i < DEVICES_COUNT; i++) {
    mutex_init(&mutex_array[i]);
  }

  if ((res = alloc_chrdev_region(&dev, 0, DEVICES_COUNT, DEVNAME)) < 0) {
    printk(KERN_ERR "membuf: failed to allocate major device number\n");
    goto module_cleanup;
    return res;
  }
  chrdev_region_allocated = true;

  pr_info("membuf: loaded, Major = %d Minor = %d\n", MAJOR(dev), MINOR(dev));

  major = MAJOR(dev);
  my_class = class_create(THIS_MODULE, "membuf_class");
  class_created = true;

  for (int i = 0; i < DEVICES_COUNT; i++) {
    my_device = MKDEV(major, i);
    cdev_init(&cdev_array[i], &operations);
    retval = cdev_add(&cdev_array[i], my_device, 1);
    if (retval < 0) {
      pr_err("membuf: cdev_init failed\n");
      goto module_cleanup;

    } else {
      device_create(my_class, NULL, my_device, NULL, "membuf%d", i);
    }
  }

  return EXIT_SUCCESS;

  module_cleanup:
    if (cdev_array != NULL) {
      kfree(cdev_array);
    }

    if (mutex_array != NULL) {
      kfree(mutex_array);
    }

    if (chrdev_region_allocated) {
      unregister_chrdev_region(dev, DEVICES_COUNT);
    }

    if (kbuffer != NULL) {
      dealocate_kbuffer();
    }

    for (i = 0; i < DEVICES_COUNT; i++) {
      if (cdev_array->dev > 0) {
        cdev_del(&cdev_array[i]);
      }
      device_destroy(my_class, my_device);
    }

    if (class_created) {
      class_destroy(my_class);
    }

    return res;
}

static void __exit kmodule_membuf_exit(void) {
  int major = MAJOR(dev);

  for (int i = 0; i < DEVICES_COUNT; i++) {
    cdev_del(&cdev_array[i]);
    device_destroy(my_class, MKDEV(major, i));
  }
  class_destroy(my_class);
  unregister_chrdev_region(dev, DEVICES_COUNT);
  dealocate_kbuffer();

  printk(KERN_INFO "membuf: Unloaded module\n");
}


module_init(kmodule_membuf_init);
module_exit(kmodule_membuf_exit);
