#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/string.h>

#define DEVNAME "membuf"
#define MAX_DEVICES_COUNT 16
#define INITIAL_DEVICES_COUNT 4
#define MAX_BUFFER_SIZE 1024
#define INITIAL_BUFFER_SIZE 256

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

static atomic_t open_devices_count = ATOMIC_INIT(0);

static ssize_t dev_read(struct file*, char*, size_t, loff_t*);
static ssize_t dev_write(struct file*, const char*, size_t, loff_t*);
static int dev_open(struct inode*, struct file*);
static int dev_release(struct inode*, struct file*);

static struct file_operations operations = {
  .owner = THIS_MODULE,
  .read = dev_read,
  .write = dev_write,
  .open = dev_open,
  .release = dev_release,
};

static struct cdev *cdev_array;
static struct mutex *mutex_array;
static struct class *my_class;

dev_t dev = 0;

static int devices_count = INITIAL_DEVICES_COUNT;
static int buffer_size_data[MAX_DEVICES_COUNT];
static char **kbuffer;

// NOTE: Single locking order guarantees lack of deadlocks.
static void acquire_exclusive_lock(void) {
  int i;

  for (i = 0; i < MAX_DEVICES_COUNT; i++) {
    mutex_lock(&mutex_array[i]);
  }
}

static void release_exclusive_lock(void) {
  int i;

  for (i = 0; i < MAX_DEVICES_COUNT; i++) {
    mutex_unlock(&mutex_array[i]);
  }
}

static int buffer_size_getter(char *buffer, const struct kernel_param *kp) {
  char data[32];
  int i;
  int offset = 0;

  for (i = 0; i < devices_count; i++) {
    sprintf(data, "%d %d\n", i, buffer_size_data[i]);
    memcpy(buffer + offset, data, strlen(data));

    offset += strlen(data);
  }

  return offset;
}

static int devices_count_setter(const char *raw_value, const struct kernel_param *param) {
  int i, previous_value, value, ret_value;
  dev_t device_spec;

  if (atomic_read(&open_devices_count) > 0) {
    return -EINVAL;
  }

  previous_value = devices_count;

  if (kstrtoint(raw_value, 10, &value) != 0) {
    printk(KERN_ERR "membuf: failed to parse 'devices_count' param value\n");
    return -EINVAL;
  }

  if (value < 1 || value > MAX_DEVICES_COUNT) {
    printk(KERN_ERR "membuf: invalid parameter 'devices_count' value\n");
    release_exclusive_lock();

    return -EINVAL;
  } else {
    printk(KERN_INFO "membuf: going to update 'devices_count' param to %d\n", value);
  }

  if (value > previous_value) {
    for (i = previous_value; i < value; i++) {
      device_spec = MKDEV(MAJOR(dev), i);
      cdev_init(&cdev_array[i], &operations);
      cdev_add(&cdev_array[i], device_spec, 1);
      device_create(my_class, NULL, device_spec, NULL, "membuf%d", i);
      kbuffer[i] = kzalloc(buffer_size_data[i], 0);
      if (kbuffer[i] == NULL) {
        printk(KERN_ERR "membuf: failed to allocate memory\n");
        while (i >= previous_value) {
          kfree(kbuffer[i]);
          cdev_del(&cdev_array[i]);
          device_destroy(my_class, MKDEV(MAJOR(dev), i));
        }
        return -EINVAL;
      }
    }
  } else if (value < previous_value) {
    for (i = value; i < previous_value; i++) {
      mutex_lock(&mutex_array[i]);
      device_spec = MKDEV(MAJOR(dev), i);
      cdev_del(&cdev_array[i]);
      device_destroy(my_class, device_spec);
      kfree(kbuffer[i]);
      mutex_unlock(&mutex_array[i]);
    }
  }

  pr_info("membuf: updated 'devices_count' param to %d\n", value);
  ret_value = param_set_uint(raw_value, param);

  if (ret_value < 0) {
    release_exclusive_lock();

    for (i = previous_value; i < value; i++) {
      kfree(kbuffer[i]);
      cdev_del(&cdev_array[i]);
    }
    return -EINVAL;
  }

  devices_count = value;

  return EXIT_SUCCESS;
}

static int buffer_size_setter(const char *raw_value, const struct kernel_param *kparam) {
  int ret_value;
  uint device_index;
  uint value;
  uint initial_value;
  char *value_buffer, *tmp_buffer, *first_token, *second_token;
  char *sep = " ";

  if (atomic_read(&open_devices_count) > 0) {
    return -EINVAL;
  }

  value_buffer = kzalloc(strlen(raw_value), 0);
  strcpy(value_buffer, raw_value);

  first_token = strsep(&value_buffer, sep);
  if (first_token == NULL) {
    printk(KERN_ERR "membuf: invalid parameter 'buffer_size_data' value\n");
    return -EINVAL;
  }

  second_token = strsep(&value_buffer, sep);
  if (second_token == NULL) {
    printk(KERN_ERR "membuf: invalid parameter 'buffer_size_data' value\n");
    return -EINVAL;
  }

  pr_info("membuf: tokens: first: %s, second: %s\n", first_token, second_token);

  ret_value = kstrtoint(first_token, 10, &device_index);
  if (ret_value < 0) {
    printk(KERN_ERR "membuf: invalid parameter 'buffer_size_data' value\n");
    return -EINVAL;
  }

  ret_value = kstrtoint(second_token, 10, &value);
  if (ret_value < 0) {
    printk(KERN_ERR "membuf: invalid parameter 'buffer_size_data' value\n");
    return -EINVAL;
  }

  if (device_index > devices_count) {
    printk(KERN_ERR "membuf: invalid parameter 'buffer_size_data' value\n");
    return -EINVAL;
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

static const struct kernel_param_ops kparam_devices_count_ops = {
  .set = devices_count_setter,
  .get = param_get_int,
};

module_param_cb(buffer_size_data, &kparam_buffer_size_ops, &buffer_size_data, S_IWUSR | S_IRUSR);
MODULE_PARM_DESC(buffer_size_data, "Per-device buffer size");

module_param_cb(devices_count, &kparam_devices_count_ops, &devices_count, S_IWUSR | S_IRUSR);
MODULE_PARM_DESC(devices_count, "Total devices count");

static int dev_open(struct inode *i, struct file *f) {
  atomic_inc(&open_devices_count);

  return EXIT_SUCCESS;
}

static int dev_release(struct inode *i, struct file *f) {
  atomic_dec(&open_devices_count);

  return EXIT_SUCCESS;
}

static ssize_t dev_write(struct file *f, const char *buffer, size_t len, loff_t* offset) {
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
    for (i = 0; i < MAX_DEVICES_COUNT; i++) {
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

  cdev_array = kmalloc(MAX_DEVICES_COUNT * sizeof(struct cdev), 0);
  mutex_array = kmalloc(MAX_DEVICES_COUNT * sizeof(struct mutex), 0);

  for (i = 0; i < MAX_DEVICES_COUNT; i++) {
    buffer_size_data[i] = INITIAL_BUFFER_SIZE;
  }

  kbuffer = (char **) kzalloc(MAX_DEVICES_COUNT * sizeof(char *), 0);
  if (kbuffer == NULL) {
    goto module_cleanup;
    printk(KERN_ERR "membuf: failed to allocate memory\n");
    return -1;
  }

  for (i = 0; i < devices_count; i++) {
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

  for (i = 0; i < devices_count; i++) {
    mutex_init(&mutex_array[i]);
  }

  if ((res = alloc_chrdev_region(&dev, 0, MAX_DEVICES_COUNT, DEVNAME)) < 0) {
    printk(KERN_ERR "membuf: failed to allocate major device number\n");
    goto module_cleanup;
    return res;
  }
  chrdev_region_allocated = true;

  pr_info("membuf: loaded, Major = %d Minor = %d\n", MAJOR(dev), MINOR(dev));

  major = MAJOR(dev);
  my_class = class_create(THIS_MODULE, "membuf_class");
  class_created = true;

  for (int i = 0; i < devices_count; i++) {
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
      unregister_chrdev_region(dev, MAX_DEVICES_COUNT);
    }

    if (kbuffer != NULL) {
      dealocate_kbuffer();
    }

    for (i = 0; i < devices_count; i++) {
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

  for (int i = 0; i < devices_count; i++) {
    cdev_del(&cdev_array[i]);
    device_destroy(my_class, MKDEV(major, i));
  }
  class_destroy(my_class);
  unregister_chrdev_region(dev, MAX_DEVICES_COUNT);
  dealocate_kbuffer();

  printk(KERN_INFO "membuf: Unloaded module\n");
}


module_init(kmodule_membuf_init);
module_exit(kmodule_membuf_exit);
