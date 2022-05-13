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
#include <linux/semaphore.h> 	/* semaphores */
#include <linux/mutex.h> 	/* mutex */
#include <linux/kernel.h>	/* printk() */
#include <linux/slab.h>		/* kmalloc() */
#include <linux/fs.h>		/* everything... */
#include <linux/errno.h>	/* error codes */
#include <linux/types.h>	/* size_t */
#include <linux/cdev.h>

#include <linux/uaccess.h>	/* copy_*_user */

#include "scull.h"		/* local definitions */
#include "access_ok_version.h"

/*
 * Our parameters which can be set at load time.
 */

static int scull_major =   SCULL_MAJOR;
static int scull_minor =   0;
static int scull_fifo_elemsz = SCULL_FIFO_ELEMSZ_DEFAULT; /* SIZE */
static int scull_fifo_size   = SCULL_FIFO_SIZE_DEFAULT; /* N */

char* FIFO_arr; 
char* start;
char* end;
module_param(scull_major, int, S_IRUGO);
module_param(scull_minor, int, S_IRUGO);
module_param(scull_fifo_size, int, S_IRUGO);
module_param(scull_fifo_elemsz, int, S_IRUGO);

MODULE_AUTHOR("Wonderful student of CS-492");
MODULE_LICENSE("Dual BSD/GPL");

static struct cdev scull_cdev;		/* Char device structure		*/

struct semaphore sem;
struct semaphore full;
struct semaphore empty;
//sema_init(&sem, 1);
//sema_init(&empty, scull_fifo_size);
//sema_init(&full, 0);
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
	return 0;          /* success */
}

/*
 * Read and Write
 */
/* consumes one element*/
static ssize_t scull_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
	/* copy(read from file) bytes of next full element into buf
	 * return the number of bytes copied as result < size of next elem
	 * if count < size of next full elem in FIFO - count bytes copied into buf is not used
	 * if no elements in array to consume - return error if copy fails
	 * compare to len instead of scull_fifo_elemsz
	 */
	
	// count is what user inputs
	/* check if count is smaller than the size of the next full elem */
	
	// Using slide 20 of Concurrency Part 2 :) and links in that same slide 
	// pls be kind :)
	
	int signal;
        signal = down_interruptible(&full); // interruptible so Ctrl C would allow user to terminate
	if (signal > 0) {
		return signal;
	}
	signal = down_interruptible(&sem);
	if (signal >0){
		up(&full);
		return signal;
	}	
	if(count < *(int*)(start)) {
		//return count; // keep count - since you're only reading count amt
	} else {
		count = *(int*)(start); // since len has less data than user asks for, give then the					    amount the is in len
	}

	/* copy_from_user - return number of bytes that could not be copied
	 * success = 0
	 */
	if (copy_to_user(buf,  start + sizeof(int), count) != 0) {
		return -EFAULT;
	}
	//check if end of the array
	//checks the index
	
	if (start - FIFO_arr == scull_fifo_size * (sizeof(int)+scull_fifo_elemsz)){
		start = FIFO_arr; //wrap to the beginning
	} else {
		// move to next element
		start = start + sizeof(int) + scull_fifo_elemsz;
	}
	up(&sem);
	up(&empty);
	return count;
}

/* produce one element */
static ssize_t scull_write(struct file *filp, const char __user *buf, size_t count,
		loff_t *f_pos)
{
	/* copy count bytes from buf into next empty FIFO element
	 *    return # of bytes copied as result - < than ELEMSZ
	 * if count > ELEMSZ then only ELEMSZ are copied
	 * else count is copied 
	 * block if no space in the array to consume
	 * error if copying fails 
	 */
	//def_start = (int)(*start); //dereference start
	//def_end = (int)(*end); //dereference end 
	int signal;
	signal = down_interruptible(&empty);
	if (signal >0) {
		return signal;
	}
	signal = down_interruptible(&sem);
	if (signal >0) {
		up(&empty);
		return signal;	
	}

	if (count >= scull_fifo_elemsz) {
		count = scull_fifo_elemsz; //count now takes elemsz, else count stays as is	
	} 	
	
	if (copy_from_user(end+sizeof(int), buf, count) != 0) {
		return -EFAULT; // essentially shifting
	} else {
		*(int*)(end) = count; // one * gets actual item, second * to cast
	}
	
	// check if you are at end of array
	if (end-FIFO_arr == scull_fifo_size * (sizeof(int) + scull_fifo_elemsz)){
		end = FIFO_arr; //wrap to the beginning
	}
	else {
		//move to next thing in start
		end= end + sizeof(int) + scull_fifo_elemsz;
	}	
	up(&sem);
	up(&full);
	return count;
}

/*
 * The ioctl() implementation
 */

static long scull_ioctl(struct file *filp, unsigned int cmd,
		unsigned long arg)
{

	int err = 0;
	int retval = 0;
    
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
	case SCULL_IOCGETELEMSZ:
		return scull_fifo_elemsz;

	default:  /* redundant, as cmd was checked against MAXNR */
		return -ENOTTY;
	}
	return retval;

}


struct file_operations scull_fops = {
	.owner 		= THIS_MODULE,
	.unlocked_ioctl = scull_ioctl,
	.open 		= scull_open,
	.release	= scull_release,
	.read 		= scull_read,
	.write 		= scull_write,
};

/*
 * Finally, the module stuff
 */

/*
 * The cleanup function is used to handle initialization failures as well.
 * Thefore, it must be careful to work correctly even if some of the items
 * have not been initialized
 */
void scull_cleanup_module(void)
{
	dev_t devno = MKDEV(scull_major, scull_minor);
	
	/* Free FIFO safely */
	kfree(FIFO_arr); /* free memory for kernel */
	/* Get rid of the char dev entry */
	cdev_del(&scull_cdev);

	/* cleanup_module is never called if registering failed */
	unregister_chrdev_region(devno, 1);
}


int scull_init_module(void)
{
	int result;
	dev_t dev = 0;
	/*char** FIFO;*/
	int len;
	sema_init(&sem, 1);
	sema_init(&empty, scull_fifo_size);
	sema_init(&full, 0);
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

	/* Allocate FIFO correctly */

	/* kmalloc_array(n elements, size elements, type of memory to allocate) */
	FIFO_arr = kmalloc_array(scull_fifo_size, scull_fifo_elemsz + sizeof(len), GFP_KERNEL);	
	start = FIFO_arr;
	end = FIFO_arr;
	printk(KERN_INFO "scull: FIFO SIZE=%u, ELEMSZ=%u\n", scull_fifo_size, 
			scull_fifo_elemsz);
	if (!FIFO_arr) {
		return -EFAULT;
	     	goto fail;	
	}
	
	return 0; /* succeed */

  fail:
	scull_cleanup_module();
	return result;
}

module_init(scull_init_module);
module_exit(scull_cleanup_module);
