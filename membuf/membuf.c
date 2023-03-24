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

static DEFINE_RWLOCK(kbuffer_rwlock);
static DEFINE_MUTEX(kbuffer_mutex);

static ssize_t dev_read(struct file*, char*, size_t, loff_t*);
static ssize_t dev_write(struct file*, const char*, size_t, loff_t*);

static struct file_operations operations = {
     .read = dev_read,
     .write = dev_write,
};


dev_t dev = 0;
static struct cdev chrdev_cdev;
static struct class *membuf_class;
static char kbuffer[BUFFER_SIZE];

static ssize_t dev_write(struct file *filep, const char *buffer, size_t
len, loff_t* offset) {
   unsigned long to_copy;

   mutex_lock(&kbuffer_mutex);

   if (*offset >= sizeof(kbuffer)) {
     mutex_unlock(&kbuffer_mutex);
     return EOF;
   }

   // write_lock(&kbuffer_rwlock);

   to_copy = MIN(len, sizeof(kbuffer));
   if (copy_from_user(kbuffer, buffer, to_copy)) {
     mutex_unlock(&kbuffer_mutex);
     return -EFAULT;
   }

   mutex_unlock(&kbuffer_mutex);
   // write_unlock(&kbuffer_rwlock);

   *offset += len;
   return len;
}

static ssize_t dev_read(struct file *filep, char *buffer, size_t len,
loff_t *offset) {
      unsigned long copy_result;
      unsigned long to_copy;

      // read_lock(&kbuffer_rwlock);
      mutex_lock(&kbuffer_mutex);

      if (*offset >= sizeof(kbuffer)) {
         mutex_unlock(&kbuffer_mutex);
         return EOF;
      }

      if (len > sizeof(kbuffer)) {
          to_copy = sizeof(kbuffer);
      } else {
          to_copy = len;
      }

      copy_result = copy_to_user(buffer, kbuffer, to_copy);
      if (copy_result) {
        mutex_unlock(&kbuffer_mutex);
        return -EFAULT;
      }

      //read_unlock(&kbuffer_rwlock);
      mutex_unlock(&kbuffer_mutex);
      *offset += to_copy;
      return to_copy;
}

static int __init kmodule_membuf_init(void) {
   int res;

   if ((res = alloc_chrdev_region(&dev, 0, 1, "chrdev")) < 0) {
     pr_err("Error allocating major number\n");
     return res;
   }

   pr_info("membuf: loaded, Major = %d Minor = %d\n", MAJOR(dev),
MINOR(dev));

   cdev_init (&chrdev_cdev, &operations);
   if ((res = cdev_add (&chrdev_cdev, dev, 1)) < 0) {
     pr_err("membuf: device registering error\n");
     unregister_chrdev_region (dev, 1);
     return res;
   }

   if (IS_ERR(membuf_class = class_create (THIS_MODULE, "membuf_class"))) {
     cdev_del (&chrdev_cdev);
     unregister_chrdev_region (dev, 1);
     return -1;
   }

   if (IS_ERR(device_create(membuf_class, NULL, dev, NULL, "membuf"))) {
     pr_err("membuf: error creating device\n");
     class_destroy (membuf_class);
     cdev_del (&chrdev_cdev);
     unregister_chrdev_region(dev, 1);
     return -1;
   } else {
     pr_info("created device membuf\n");
   }

   return EXIT_SUCCESS;
}

static void __exit kmodule_membuf_exit(void) {
     device_destroy (membuf_class, dev);
     class_destroy (membuf_class);
     cdev_del (&chrdev_cdev);
     unregister_chrdev_region(dev, 1);

     kfree(kbuffer);

     printk(KERN_INFO "Unloaded module membuf\n");
}


module_init(kmodule_membuf_init);
module_exit(kmodule_membuf_exit);
