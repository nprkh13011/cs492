/*
 * main.c -- the bare scull char module
 *
 * Copyright (C) 2001 Alessandro Rubini and Jonathan Corbet
 * Copyright (C) 2001 O'Reilly & Associates
 *
 * The source code in this file can be freely used, adapted,
 * and redistributed in source or binary form, so long as an
 * acknowledgment appears in derived source files.  The citation
 * should list that the code comes from the book "Linux Device
 * Drivers" by Alessandro Rubini and Jonathan Corbet, published
 * by O'Reilly & Associates.   No warranty is attached;
 * we cannot take responsibility for errors or fitness for use.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>

#include <linux/kernel.h>	/* printk() */
#include <linux/slab.h>		/* kmalloc() */
#include <linux/fs.h>		/* everything... */
#include <linux/errno.h>	/* error codes */
#include <linux/types.h>	/* size_t */
#include <linux/cdev.h>

#include <linux/uaccess.h>	/* copy_*_user */
#include <linux/mutex.h> 	/* mutex */

#include "scull.h"		/* local definitions */
#include "access_ok_version.h"

/*
 * Our parameters which can be set at load time.
 */

static int scull_major =   SCULL_MAJOR;
static int scull_minor =   0;
static int scull_quantum = SCULL_QUANTUM;
static DEFINE_MUTEX(mutex);

module_param(scull_major, int, S_IRUGO);
module_param(scull_minor, int, S_IRUGO);
module_param(scull_quantum, int, S_IRUGO);

MODULE_AUTHOR("Wonderful student of CS-492");
MODULE_LICENSE("Dual BSD/GPL");

static struct cdev scull_cdev;		/* Char device structure		*/
struct task_info output; 		/* initialize a task_info struct */
struct linked_list* ll;			/* initialize a linked_list struct */ 
/*
 * Open and close
 */

static int scull_open(struct inode *inode, struct file *filp)
{
	printk(KERN_INFO "scull open\n");
	return 0;          /* success */
}

static int scull_release(struct inode *inode, struct file *filp)
{
	printk(KERN_INFO "scull close\n");
	return 0;
}


/*
 * The ioctl() implementation
 */

static long scull_ioctl(struct file *filp, unsigned int cmd,
		unsigned long arg)
{
	int err = 0, tmp;
	int retval = 0;
	struct linked_list *node;
	int flag = 0; /*see if the node in linked list exists */
    	
	/*
	 * extract the type and number bitfields, and don't decode
	 * wrong cmds: return ENOTTY (inappropriate ioctl) before access_ok()
	 */
	if (_IOC_TYPE(cmd) != SCULL_IOC_MAGIC) return -ENOTTY;
	if (_IOC_NR(cmd) > SCULL_IOC_MAXNR) return -ENOTTY;

	/*
	 * the direction is a bitmask, and VERIFY_WRITE catches R/W
	 * transfers. `Type' is user-oriented, while
	 * access_ok is kernel-oriented, so the concept of "read" and
	 * "write" is reversed
	 */
	if (_IOC_DIR(cmd) & _IOC_READ)
		err = !access_ok_wrapper(VERIFY_WRITE, (void __user *)arg,
				_IOC_SIZE(cmd));
	else if (_IOC_DIR(cmd) & _IOC_WRITE)
		err =  !access_ok_wrapper(VERIFY_READ, (void __user *)arg,
				_IOC_SIZE(cmd));
	if (err) return -EFAULT;

	switch(cmd) {

	case SCULL_IOCRESET:
		scull_quantum = SCULL_QUANTUM;
		break;
        
	case SCULL_IOCSQUANTUM: /* Set: arg points to the value */
		retval = __get_user(scull_quantum, (int __user *)arg);
		break;

	case SCULL_IOCTQUANTUM: /* Tell: arg is the value */
		scull_quantum = arg;
		break;

	case SCULL_IOCGQUANTUM: /* Get: arg is pointer to result */
		retval = __put_user(scull_quantum, (int __user *)arg);
		break;

	case SCULL_IOCQQUANTUM: /* Query: return it (it's positive) */
		return scull_quantum;

	case SCULL_IOCXQUANTUM: /* eXchange: use arg as pointer */
		tmp = scull_quantum;
		retval = __get_user(scull_quantum, (int __user *)arg);
		if (retval == 0)
			retval = __put_user(tmp, (int __user *)arg);
		break;

	case SCULL_IOCHQUANTUM: /* sHift: like Tell + Query */
		tmp = scull_quantum;
		scull_quantum = arg;
		return tmp;

	case SCULL_IOCIQUANTUM: /* Info */
	
		mutex_lock(&mutex); /* for concurrency purposes */
		/* Critical section */
		if (ll) { /* if you initialize the linked_list */	
			node = ll;
			
		 /* traverse thru the linked list 
			 see if pid & tgid exist as a pair */
		do {
			if ((node->pid)==current->pid){
				if ((node->tgid)==current->tgid){
					flag = -1;
					break;		
				}	       
			}
			if (node->next){
				node = node->next;
			} else {
				break;
			}

		} while (node->next);
		/*the linked list part -- basically only adding to the tail part */
		if(flag==0) { /*when flag is not used, then it can add to linked list */ 
			node->next = kmalloc(sizeof(struct linked_list), GFP_KERNEL); /*tail */
			node->next->next = NULL; /* fix weird segfault */
			node->next->prev = node; /*points back to current node-previous tail*/
			node->next->pid = current->pid; /*gets pid of tail*/
			node->next->tgid = current->tgid; /*gets tgid of tail*/
		}
	} else { /*instantiate linked list*/
		ll = kmalloc(sizeof(struct linked_list),GFP_KERNEL);
		ll->next = NULL;
		ll->prev = NULL;
		ll->pid = current->pid;
		ll->tgid = current->tgid;
		
	}
		mutex_unlock(&mutex);
		/* using the macro 'current', assign values to the 
		 * struct task_info 
		 * put_user() -- copies single interger
		 * copy_to_user() -- used to copy structure/bytes
		 * */
		output.state = current->state;
		output.stack = current->stack;
		output.cpu = current->cpu;
		output.prio = current->prio;
		output.static_prio = current->static_prio;
		output.normal_prio = current->normal_prio;
		output.rt_priority = current->rt_priority;
		output.pid = current->pid;
		output.tgid = current->tgid;
		output.nvcsw = current->nvcsw;
		output.nivcsw = current->nivcsw;
		/* this will copy the data from kernel (driver) to user(src) 
		 * copy_to_user(to, from, n)
		 * copy into memory address
		 * */
		retval = copy_to_user((int __user *) arg, &output, sizeof(output));
		break;

	default:  /* redundant, as cmd was checked against MAXNR */
		return -ENOTTY;
	}
	return retval;

}


struct file_operations scull_fops = {
	.owner =    THIS_MODULE,
	.unlocked_ioctl = scull_ioctl,
	.open =     scull_open,
	.release =  scull_release,
};

/*
 * Finally, the module stuff
 */

/*
 * The cleanup function is used to handle initialization failures as well.
 * Thefore, it must be careful to work correctly even if some of the items
 * have not been initialized
 *
 * step 1: print the traversal 
 * step 2: free the traversal
 */
void scull_cleanup_module(void){		
	int count = 1;
	struct linked_list* node = ll;
	struct linked_list* tmp; /* by creating a tmp variable, node wont have to be NULL */
	/*traversal part again*/
	while(node != NULL) {
		printk(KERN_INFO "Task %d: PID %d, TGID %d\n", count++, node->pid, node->tgid);
		tmp = node;
			node = node->next;
		kfree(tmp);
	}
	

	dev_t devno = MKDEV(scull_major, scull_minor);

	/* Get rid of the char dev entry */
	cdev_del(&scull_cdev);

	/* cleanup_module is never called if registering failed */
	unregister_chrdev_region(devno, 1);
	
	
}


int scull_init_module(void)
{
	int result;
	dev_t dev = 0;

	/*
	 * Get a range of minor numbers to work with, asking for a dynamic
	 * major unless directed otherwise at load time.
	 */
	if (scull_major) {
		dev = MKDEV(scull_major, scull_minor);
		result = register_chrdev_region(dev, 1, "scull");
	} else {
		result = alloc_chrdev_region(&dev, scull_minor, 1, "scull");
		scull_major = MAJOR(dev);
	}
	if (result < 0) {
		printk(KERN_WARNING "scull: can't get major %d\n", scull_major);
		return result;
	}

	cdev_init(&scull_cdev, &scull_fops);
	scull_cdev.owner = THIS_MODULE;
	result = cdev_add (&scull_cdev, dev, 1);
	/* Fail gracefully if need be */
	if (result) {
		printk(KERN_NOTICE "Error %d adding scull character device", result);
		goto fail;
	}

	return 0; /* succeed */

  fail:
	scull_cleanup_module();
	return result;
}

module_init(scull_init_module);
module_exit(scull_cleanup_module);
