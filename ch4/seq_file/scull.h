#ifndef _SCULL_H_
#define _SCULL_H_

#ifndef SCULL_MAJOR
#define SCULL_MAJOR 0 /* dynamic major by default */
#endif

#ifndef SCULL_NR_DEVS
#define SCULL_NR_DEVS 4 /* scull0 through scull3 */
#endif

#ifndef SCULL_QUANTUM
#define SCULL_QUANTUM 4000
#endif

#ifndef SCULL_QSET
#define SCULL_QSET 1000
#endif

#define SCULL_DEBUG

#define proc_ops_wrapper(fops, newname)                             \
    ({                                                              \
        static struct proc_ops newname;                             \
                                                                    \
        newname.proc_open = (fops)->open;                           \
        newname.proc_read = (fops)->read;                           \
        newname.proc_write = (fops)->write;                         \
        newname.proc_release = (fops)->release;                     \
        newname.proc_poll = (fops)->poll;                           \
        newname.proc_ioctl = (fops)->unlocked_ioctl;                \
        newname.proc_mmap = (fops)->mmap;                           \
        newname.proc_get_unmapped_area = (fops)->get_unmapped_area; \
        newname.proc_lseek = (fops)->llseek;                        \
        &newname;                                                   \
    })


/*
 * Representation of scull quantum sets.
 */
struct scull_qset {
    void **data;
    struct scull_qset *next;
};

struct scull_dev {
    struct scull_qset *data; // Pointer to first quantum set
    int quantum;             // the current quantum size
    int qset;                // the current array size
    unsigned long size;      // amount of data stored here
    struct mutex lock;       // mutual exclusion semaphore
    struct cdev cdev;        // Char device structure
};

#endif // _SCULL_H_
