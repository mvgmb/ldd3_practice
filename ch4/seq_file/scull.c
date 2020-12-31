#include <linux/init.h> // module_init and exit
#include <linux/module.h>
#include <linux/moduleparam.h> // module_param

#include <linux/cdev.h>   // cdev definition
#include <linux/fs.h>     // register_chrdev_region, file_operations, everything
#include <linux/kdev_t.h> // macros MAJOR, MINOR, MKDEV...
#include <linux/kernel.h> // printk
#include <linux/proc_fs.h>
#include <linux/sched.h> // current->
#include <linux/seq_file.h>
#include <linux/slab.h>  // kmalloc(),kfree()
#include <linux/types.h> // dev_t type

#include "scull.h"

int scull_major = SCULL_MAJOR;
int scull_minor = 0;
int scull_nr_devs = SCULL_NR_DEVS;
int scull_quantum = SCULL_QUANTUM;
int scull_qset = SCULL_QSET;

module_param(scull_major, int, S_IRUGO);
module_param(scull_minor, int, S_IRUGO);
module_param(scull_nr_devs, int, S_IRUGO);
module_param(scull_quantum, int, S_IRUGO);
module_param(scull_qset, int, S_IRUGO);

struct scull_dev *scull_devices;


#ifdef SCULL_DEBUG

/*
 * Here are our sequence iteration methods.  Our "position" is
 * simply the device number.
 */

static void *scull_seq_start(struct seq_file *s, loff_t *pos) {
    if (*pos >= scull_nr_devs)
        return NULL;
    return scull_devices + *pos;
}

static void *scull_seq_next(struct seq_file *s, void *v, loff_t *pos) {
    (*pos)++;
    if (*pos >= scull_nr_devs)
        return NULL;
    return scull_devices + *pos;
}

static void scull_seq_stop(struct seq_file *s, void *v) {
    // Nothing to do here
}

static int scull_seq_show(struct seq_file *s, void *v) {
    struct scull_dev *dev = (struct scull_dev *) v;
    struct scull_qset *d;
    int i;

    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;
    seq_printf(s, "\nDevice %i: qset %i, q %i, sz %li\n",
               (int) (dev - scull_devices), dev->qset,
               dev->quantum, dev->size);
    for (d = dev->data; d; d = d->next) {
        seq_printf(s, "  item at %p, qset at %p\n", d, d->data);
        // dump only the last item
        if (d->data && !d->next)
            for (i = 0; i < dev->qset; i++) {
                if (d->data[i])
                    seq_printf(s, "    % 4i: %8p\n",
                               i, d->data[i]);
            }
    }
    mutex_unlock(&dev->lock);
    return 0;
}

/*
 * Tie the sequence operators up.
 */

static struct seq_operations scull_seq_ops = {
        .start = scull_seq_start,
        .next = scull_seq_next,
        .stop = scull_seq_stop,
        .show = scull_seq_show,
};

/*
 * Now to implement the /proc files we need only make an open
 * method which sets up the sequence operators.
 */

static int scullseq_proc_open(struct inode *inode, struct file *file) {
    return seq_open(file, &scull_seq_ops);
}

/*
 * Create a set of file operations for our proc files.
 */

static struct file_operations scullseq_proc_ops = {
        .owner = THIS_MODULE,
        .open = scullseq_proc_open,
        .read = seq_read,
        .llseek = seq_lseek,
        .release = seq_release,
};

static void scull_create_proc(void) {
    proc_create("scullseq", 0, NULL, proc_ops_wrapper(&scullseq_proc_ops, scullseq_pops));
}

static void scull_remove_proc(void) {
    // no problem if it was not registered
    remove_proc_entry("scullseq", NULL);
}


#endif // SCULL_DEBUG

/*
 * Empty out the scull device; must be called with the device
 * semaphore held.
 */

int scull_trim(struct scull_dev *dev) {
    struct scull_qset *next, *dptr;
    // "dev" is not-null
    int qset = dev->qset;
    int i;

    // all the list items
    for (dptr = dev->data; dptr; dptr = next) {
        if (dptr->data) {
            for (i = 0; i < qset; i++)
                kfree(dptr->data[i]);
            kfree(dptr->data);
            dptr->data = NULL;
        }
        next = dptr->next;
        kfree(dptr);
    }
    dev->size = 0;
    dev->quantum = scull_quantum;
    dev->qset = scull_qset;
    dev->data = NULL;
    return 0;
}

/*
 * Open and close
 */

int scull_open(struct inode *inode, struct file *filp) {
    // device information
    struct scull_dev *dev;

    printk(KERN_INFO "scull: open\n");


    dev = container_of(inode->i_cdev, struct scull_dev, cdev);
    //for other methods
    filp->private_data = dev;

    // now trim to 0 the length of the device if open was write-only
    if ((filp->f_flags & O_ACCMODE) == O_WRONLY) {
        if (mutex_lock_interruptible(&dev->lock))
            return -ERESTARTSYS;
        scull_trim(dev); //ignore errors
        mutex_unlock(&dev->lock);
    }
    return 0;
}

int scull_release(struct inode *inode, struct file *filp) {
    printk(KERN_INFO "scull: release\n");
    return 0;
}

/*
 * Follow the list
 */

struct scull_qset *scull_follow(struct scull_dev *dev, int n) {
    struct scull_qset *qs = dev->data;

    if (!qs) {
        // Allocate first qset explicitly
        qs = dev->data = kmalloc(sizeof(struct scull_qset), GFP_KERNEL);
        if (qs == NULL)
            return NULL;
        memset(qs, 0, sizeof(struct scull_qset));
    }

    // Follow the list
    while (n--) {
        if (!qs->next) {
            qs->next = kmalloc(sizeof(struct scull_qset), GFP_KERNEL);
            if (qs->next == NULL)
                return NULL;
            memset(qs->next, 0, sizeof(struct scull_qset));
        }
        qs = qs->next;
        continue;
    }
    return qs;
}

/*
 * Data management: read and write
 */

ssize_t scull_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos) {
    struct scull_dev *dev = filp->private_data;
    struct scull_qset *dptr; // the first listitem
    int quantum = dev->quantum, qset = dev->qset;
    int itemsize = quantum * qset; // how many bytes in the listitem
    int item, s_pos, q_pos, rest;
    ssize_t retval = 0;

    printk(KERN_INFO "scull: read\n");

    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;
    if (*f_pos >= dev->size)
        goto out;
    if (*f_pos + count > dev->size)
        count = dev->size - *f_pos;

    // find listitem, qset index, and offset in the quantum
    item = (long) *f_pos / itemsize;
    rest = (long) *f_pos % itemsize;
    s_pos = rest / quantum;
    q_pos = rest % quantum;

    // follow the list up to the right position
    dptr = scull_follow(dev, item);

    if (dptr == NULL || !dptr->data || !dptr->data[s_pos])
        goto out; // don't fill holes

    // read only up to the end of this quantum
    if (count > quantum - q_pos)
        count = quantum - q_pos;

    if (copy_to_user(buf, dptr->data[s_pos] + q_pos, count)) {
        retval = -EFAULT;
        goto out;
    }
    *f_pos += count;
    retval = count;

out:
    mutex_unlock(&dev->lock);
    return retval;
}

ssize_t scull_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos) {
    struct scull_dev *dev = filp->private_data;
    struct scull_qset *dptr;
    int quantum = dev->quantum, qset = dev->qset;
    int itemsize = quantum * qset;
    int item, s_pos, q_pos, rest;
    ssize_t retval = -ENOMEM;

    printk(KERN_INFO "scull: write\n");

    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;

    // find listitem, qset index and offset in the quantum
    item = (long) *f_pos / itemsize;
    rest = (long) *f_pos % itemsize;
    s_pos = rest / quantum;
    q_pos = rest % quantum;

    // follow the list up to the right position
    dptr = scull_follow(dev, item);
    if (dptr == NULL)
        goto out;
    if (!dptr->data) {
        dptr->data = kmalloc(qset * sizeof(char *), GFP_KERNEL);
        if (!dptr->data)
            goto out;
        memset(dptr->data, 0, qset * sizeof(char *));
    }
    if (!dptr->data[s_pos]) {
        dptr->data[s_pos] = kmalloc(quantum, GFP_KERNEL);
        if (!dptr->data[s_pos])
            goto out;
    }
    // write only up to the end of this quantum
    if (count > quantum - q_pos)
        count = quantum - q_pos;

    if (copy_from_user(dptr->data[s_pos] + q_pos, buf, count)) {
        retval = -EFAULT;
        goto out;
    }
    *f_pos += count;
    retval = count;

    // update the size
    if (dev->size < *f_pos)
        dev->size = *f_pos;

out:
    mutex_unlock(&dev->lock);
    return retval;
}

/*
 * Create a set of file operations for our scull files.
 * All the functions do nothig
 */

struct file_operations scull_fops = {
        .owner = THIS_MODULE,
        .read = scull_read,
        .write = scull_write,
        .open = scull_open,
        .release = scull_release,
};

/*
 * The cleanup function is used to handle initialization failures as well.
 * Thefore, it must be careful to work correctly even if some of the items
 * have not been initialized
 */

void scull_cleanup_module(void) {
    int i;
    dev_t devno = MKDEV(scull_major, scull_minor);

    // Get rid of our char dev entries
    if (scull_devices) {
        for (i = 0; i < scull_nr_devs; i++) {
            scull_trim(scull_devices + i);
            cdev_del(&scull_devices[i].cdev);
        }
        kfree(scull_devices);
    }

#ifdef SCULL_DEBUG
    scull_remove_proc();
#endif // SCULL_DEBUG

    // cleanup_module is never called if registering failed
    unregister_chrdev_region(devno, scull_nr_devs);
}

/*
 * Set up the char_dev structure for this device.
 */

static void scull_setup_cdev(struct scull_dev *dev, int index) {
    int err, devno = MKDEV(scull_major, scull_minor + index);

    cdev_init(&dev->cdev, &scull_fops);
    dev->cdev.owner = THIS_MODULE;
    err = cdev_add(&dev->cdev, devno, 1);
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

    printk(KERN_INFO "scull: init\n");

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

    // Initialize each device.
    for (i = 0; i < scull_nr_devs; i++) {
        scull_devices[i].quantum = scull_quantum;
        scull_devices[i].qset = scull_qset;
        mutex_init(&scull_devices[i].lock);
        scull_setup_cdev(&scull_devices[i], i);
    }

#ifdef SCULL_DEBUG
    scull_create_proc();
#endif // SCULL_DEBUG

    return 0;

fail:
    scull_cleanup_module();
    return result;
}

/*
 * The exit function is simply calls the cleanup
 */

static void scull_exit(void) {
    printk(KERN_INFO "scull: exit\n");
    scull_cleanup_module();
}

module_init(scull_init);
module_exit(scull_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Mário Bezerra");
MODULE_DESCRIPTION("testing read and write functions");
MODULE_VERSION("1.0");
