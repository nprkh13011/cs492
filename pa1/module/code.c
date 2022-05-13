
#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <asm/current.h>
static int hello_init(void) {
	printk(KERN_ALERT "Hello World from NIDHI 10452568\n");
	return 0;
}
static void hello_exit(void) {
	printk(KERN_ALERT "Task \"%s\" (pid %i)\n", current->comm,current->pid);

}
module_init(hello_init);
module_exit(hello_exit);
MODULE_LICENSE("Dual BSD/GPL");
