#include <linux/fs.h>     /* needed for register_chrdev_region */
#include <linux/init.h>   /* needed for module_init and exit */
#include <linux/kdev_t.h> /* needed for macros MAJOR, MINOR, MKDEV... */
#include <linux/kernel.h> /* needed for printk */
#include <linux/module.h>
#include <linux/moduleparam.h> /* needed for module_param */
#include <linux/sched.h>       /* needed for current-> */
#include <linux/types.h>       /* needed for dev_t type */

int hello_major = 0;
int hello_minor = 0;
unsigned int hello_nr_devs = 1;

module_param(hello_major, int, S_IRUGO);
module_param(hello_minor, int, S_IRUGO);
module_param(hello_nr_devs, int, S_IRUGO);

/*
 * The init function is used to register the chdeiv allocating dinamically a
 * new major number (if not specified at load/compilation-time)
 */
static int hello_init(void) {
    int result = 0;
    dev_t dev = 0;

    printk(KERN_ALERT "hello_init\n");
    printk(KERN_INFO "default values of (major, minor) == (%d, %d)\n", hello_major, hello_minor);

    if (hello_major) {
        printk(KERN_INFO "static allocation of major number (%d)\n", hello_major);
        dev = MKDEV(hello_major, hello_minor);
        result = register_chrdev_region(dev, hello_nr_devs, "hello");
    } else {
        printk(KERN_INFO "dinamic allocation of major number\n");
        result = alloc_chrdev_region(&dev, hello_minor, hello_nr_devs, "hello");
        hello_major = MAJOR(dev);
    }
    if (result < 0) {
        printk(KERN_WARNING "hello: can't get major %d\n", hello_major);
        return result;
    }
    printk(KERN_INFO "values of (major, minor) == (%d, %d)\n", hello_major, hello_minor);
    return 0;
}

/*
 * The cleanup function is used to handle initialization failures as well.
 * Thefore, it must be careful to work correctly even if some of the items
 * have not been initialized
 */
void hello_cleanup_module(void) {
    dev_t devno = MKDEV(hello_major, hello_minor);
    unregister_chrdev_region(devno, hello_nr_devs);
}

/*
 * The exit function is simply calls the cleanup
 */
static void hello_exit(void) {
    printk(KERN_ALERT "hello_exit\n");
    hello_cleanup_module();
}

module_init(hello_init);
module_exit(hello_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Mário Bezerra");
MODULE_DESCRIPTION("test module_param");
MODULE_VERSION("1.0");
