#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>

#define DEVNAME "membuf"
#define EXIT_SUCCESS 0
#define EOF 0
#define BUFFER_SIZE 1024

#define MAX(x, y) (((x) > (y)) ? (x) : (y));
#define MIN(x, y) (((x) < (y)) ? (x) : (y));

MODULE_LICENSE("GPL");
MODULE_AUTHOR("yk");
MODULE_DESCRIPTION("Membuf kernel module");
MODULE_VERSION("1.0.0");

static int devices_count;
module_param(devices_count, int, 0600);
MODULE_PARM_DESC(devices_count, "Total amount of devices available");

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
static char kbuffer[BUFFER_SIZE];

static ssize_t dev_write(struct file *f, const char *buffer, size_t
len, loff_t* offset) {
   unsigned long to_copy;
   int minor = iminor(file_inode(f));

   mutex_lock(&mutex_array[minor]);

   if (*offset >= sizeof(kbuffer)) {
     mutex_unlock(&mutex_array[minor]);
     return EOF;
   };

   to_copy = MIN(len, sizeof(kbuffer));
   if (copy_from_user(kbuffer, buffer, to_copy)) {
     mutex_unlock(&mutex_array[minor]);
     return -EFAULT;
   }

   mutex_unlock(&mutex_array[minor]);

   *offset += len;
   return len;
}

static ssize_t dev_read(struct file *filep, char *buffer, size_t len,
loff_t *offset) {
      unsigned long copy_result;
      unsigned long to_copy;
      int minor = iminor(file_inode(filep));
      mutex_lock(&mutex_array[minor]);

      if (*offset >= sizeof(kbuffer)) {
         mutex_unlock(&mutex_array[minor]);
         return EOF;
      }

      if (len > sizeof(kbuffer)) {
          to_copy = sizeof(kbuffer);
      } else {
          to_copy = len;
      }

      copy_result = copy_to_user(buffer, kbuffer, to_copy);
      if (copy_result) {
        mutex_unlock(&mutex_array[minor]);
        return -EFAULT;
      }

      mutex_unlock(&mutex_array[minor]);
      *offset += to_copy;
      return to_copy;
}

static int __init kmodule_membuf_init(void) {
   int res;
   int i;

   if (devices_count == 0) {
     devices_count = 32;
   }

   pr_info("membuf: devices_count = %d", devices_count);

   cdev_array = kmalloc(devices_count * sizeof(struct cdev), 0);
   mutex_array = kmalloc(devices_count * sizeof(struct mutex), 0);

   for (i = 0; i < devices_count; i++) {
     mutex_init(&mutex_array[i]);
    }

   if ((res = alloc_chrdev_region(&dev, 0, devices_count, "membuf")) < 0) {
     pr_err("Error allocating major number\n");
     return res;
   }

   pr_info("membuf: loaded, Major = %d Minor = %d\n", MAJOR(dev), MINOR(dev));

   int retval = -1;
   int major = MAJOR(dev);
   dev_t my_device;

   my_class = class_create(THIS_MODULE, "membuf_class");
   for (int i = 0; i < devices_count; i++) {
     my_device = MKDEV(major, i);
     cdev_init(&cdev_array[i], &operations);
     retval = cdev_add(&cdev_array[i], my_device, 1);
     if (retval < 0) {
       pr_err("membuf: cdev_init failed\n");
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

     for (int i = 0; i < devices_count; i++) {
       my_device = MKDEV(major, i);
       cdev_del(&cdev_array[i]);
       device_destroy(my_class, my_device);
     }
     class_destroy(my_class);

     unregister_chrdev_region(dev, devices_count);

     kfree(kbuffer);

     printk(KERN_INFO "Unloaded module membuf\n");
}


module_init(kmodule_membuf_init);
module_exit(kmodule_membuf_exit);
