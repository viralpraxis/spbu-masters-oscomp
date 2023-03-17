#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>

#define DEVNAME "nulldump"
#define EXIT_SUCCESS 0
#define EOF 0

MODULE_LICENSE("GPL");
MODULE_AUTHOR("yk");
MODULE_DESCRIPTION("Nulldump kernel module");
MODULE_VERSION("1.0.0");

static ssize_t dev_read(struct file*, char*, size_t, loff_t*);
static ssize_t dev_write(struct file*, const char*, size_t, loff_t*);

static struct file_operations operations = {
     .read = dev_read,
     .write = dev_write,
};

dev_t dev = 0;
static struct cdev chrdev_cdev;
static struct class *nulldump_class;
static char *kbuffer = NULL;

static ssize_t dev_write(struct file *filep, const char *buffer, size_t len, loff_t*) {
     int copy_result;
     char *prefix;

     kbuffer = kmalloc(len, 0);
     copy_result = copy_from_user(kbuffer, buffer, len);

     if (copy_result == 0) {
       prefix = kmalloc(64 + strlen(current->comm), 0); // 64 bytes are enough for const-sized string and pid
       sprintf(prefix, "nulldump write: pid: %i, comm: (%s)", current->pid, current->comm);

       print_hex_dump(KERN_INFO, prefix, DUMP_PREFIX_NONE, 16, 1, kbuffer, len + 1, 1);
     } else {
       printk(KERN_INFO "failed to read from user\n, bytes read failed count: %d\n", copy_result);
     }

     if (prefix) {
       kfree(prefix);
     }

     return len;
}

static ssize_t dev_read(struct file *filep, char *buffer, size_t len, loff_t *offset) {
      printk(KERN_INFO "nulldump: read performed\n");

      return EOF;
}

static int __init kmodule_nulldump_init(void) {
   int res;

   if ((res = alloc_chrdev_region(&dev, 0, 1, "chrdev")) < 0) {
     pr_err("Error allocating major number\n");
     return res;
   }

   pr_info("nulldump: loaded, Major = %d Minor = %d\n", MAJOR(dev), MINOR(dev));

   cdev_init (&chrdev_cdev, &operations);
   if ((res = cdev_add (&chrdev_cdev, dev, 1)) < 0) {
     pr_err("nulldump: device registering error\n");
     unregister_chrdev_region (dev, 1);
     return res;
   }

   if (IS_ERR(nulldump_class = class_create (THIS_MODULE, "nulldump_class"))) {
     cdev_del (&chrdev_cdev);
     unregister_chrdev_region (dev, 1);
     return -1;
   }

   if (IS_ERR(device_create(nulldump_class, NULL, dev, NULL, "nulldump"))) {
     pr_err("nulldump: error creating device\n");
     class_destroy (nulldump_class);
     cdev_del (&chrdev_cdev);
     unregister_chrdev_region(dev, 1);
     return -1;
   } else {
     pr_info("created device nulldump\n");
   }

   return EXIT_SUCCESS;
}

static void __exit kmodule_nulldump_exit(void) {
     device_destroy (nulldump_class, dev);
     class_destroy (nulldump_class);
     cdev_del (&chrdev_cdev);
     unregister_chrdev_region(dev, 1);

     if (kbuffer) {
       kfree(kbuffer);
     }

     printk(KERN_INFO "Unloaded module nulldump\n");
}


module_init(kmodule_nulldump_init);
module_exit(kmodule_nulldump_exit);
