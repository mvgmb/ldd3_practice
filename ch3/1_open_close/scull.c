
#include <linux/cdev.h>   /* cdev definition */
#include <linux/fs.h>     /* needed for register_chrdev_region, file_operations */
#include <linux/init.h>   /* needed for module_init and exit */
#include <linux/kdev_t.h> /* needed for macros MAJOR, MINOR, MKDEV... */
#include <linux/kernel.h> /* needed for printk */
#include <linux/module.h>
#include <linux/moduleparam.h> /* needed for module_param */
#include <linux/sched.h>       /* needed for current-> */
#include <linux/slab.h>        /* kmalloc(),kfree() */
#include <linux/types.h>       /* needed for dev_t type */

#include "scull.h"


int scull_major = SCULL_MAJOR;
int scull_minor = 0;
int scull_nr_devs = SCULL_NR_DEVS; /* number of bare scull devices */
int scull_quantum = SCULL_QUANTUM;
int scull_qset = SCULL_QSET;

module_param(scull_major, int, S_IRUGO);
module_param(scull_minor, int, S_IRUGO);
module_param(scull_nr_devs, int, S_IRUGO);
module_param(scull_quantum, int, S_IRUGO);
module_param(scull_qset, int, S_IRUGO);

struct scull_dev *scull_devices; /* allocated in scull_init_module */

/*
 * The cleanup function is used to handle initialization failures as well.
 * Thefore, it must be careful to work correctly even if some of the items
 * have not been initialized
 */

void scull_cleanup_module(void) {
    dev_t devno = MKDEV(scull_major, scull_minor);
    cdev_del(&(scull_devices->cdev));
    kfree(scull_devices);
    unregister_chrdev_region(devno, scull_nr_devs);
}

/*
 * Open and close
 */

int scull_open(struct inode *inode, struct file *filp) {
    struct scull_dev *dev;

    printk(KERN_INFO "open operation\n");
    dev = container_of(inode->i_cdev, struct scull_dev, cdev);
    filp->private_data = dev;
    return 0;
}

int scull_release(struct inode *inode, struct file *filp) {
    printk(KERN_INFO "release operation\n");
    return 0;
}

/*
 * Create a set of file operations for our scull files.
 * All the functions do nothig
 */

struct file_operations scull_fops = {
        .owner = THIS_MODULE,
        .open = scull_open,
        .release = scull_release,
};

/*
 * Set up the char_dev structure for this device.
 */

static void scull_setup_cdev(struct scull_dev *dev, int index) {
    int err, devno = MKDEV(scull_major, scull_minor + index);

    cdev_init(&dev->cdev, &scull_fops);
    dev->cdev.owner = THIS_MODULE;
    err = cdev_add(&dev->cdev, devno, 1);
    /* Fail gracefully if need be */
    if (err)
        printk(KERN_NOTICE "Error %d adding scull%d", err, index);
}

/*
 * The init function is used to register the chdeiv allocating dinamically a
 * new major number (if not specified at load/compilation-time)
 */

static int scull_init(void) {
    int result, i;
    dev_t dev = 0;

    printk(KERN_ALERT "scull_init\n");

    if (scull_major) {
        dev = MKDEV(scull_major, scull_minor);
        result = register_chrdev_region(dev, scull_nr_devs, "scull");
    } else {
        result = alloc_chrdev_region(&dev, scull_minor, scull_nr_devs, "scull");
        scull_major = MAJOR(dev);
    }
    if (result < 0) {
        return result;
    }

    scull_devices = kmalloc(scull_nr_devs * sizeof(struct scull_dev), GFP_KERNEL);
    if (!scull_devices) {
        result = -ENOMEM;
        goto fail;
    }
    memset(scull_devices, 0, scull_nr_devs * sizeof(struct scull_dev));

    /* Initialize each device. */
    for (i = 0; i < scull_nr_devs; i++) {
        scull_devices[i].quantum = scull_quantum;
        scull_devices[i].qset = scull_qset;
        mutex_init(&scull_devices[i].lock);
        scull_setup_cdev(&scull_devices[i], i);
    }

    return 0;

fail:
    scull_cleanup_module();
    return result;
}

/*
 * The exit function is simply calls the cleanup
 */

static void scull_exit(void) {
    printk(KERN_ALERT "scull_exit\n");
    scull_cleanup_module();
}

module_init(scull_init);
module_exit(scull_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Mário Bezerra");
MODULE_DESCRIPTION("testing open and close functions");
MODULE_VERSION("1.0");
