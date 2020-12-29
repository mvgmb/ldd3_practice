#include <linux/init.h>
#include <linux/module.h>

static int hello_init(void) {
    printk(KERN_ALERT "Hello, world\n");
    return 0;
}

static void hello_exit(void) {
    printk(KERN_ALERT "Goodbye, cruel world\n");
}

module_init(hello_init);
module_exit(hello_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Mário Bezerra");
MODULE_DESCRIPTION("An hello worlk module for demonstration");
MODULE_VERSION("1.0");