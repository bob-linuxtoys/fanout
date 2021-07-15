/*
 * fanout.c:  A one-to-many multiplexer
 *
 * Copyright (C) 2010-2021, Bob Smith, Frederic Roussel
 * This software is released under your choice of either
 * the GPLv2 or the 3-clause BSD license.
 * 
 * Initial release: Bob Smith
 * changes, added more locking: Edwin van den Oetelaar (www.oetelaar.com)
 *          added automatic mknod: Frederic Roussel (fr.frasc@gmail.com)
 */

/* Comment out to forgo the creation of /dev entries
 * The companion udev rules 'fanout.rules' sets the special file mode
 */
#define DEV_MKNOD

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <asm/uaccess.h>
#ifdef DEV_MKNOD
#  include <linux/device.h>
#  include <linux/device/class.h>
#endif /* DEV_MKNOD */


/* Limits and other defines */
/* The # fanout devices.  Max minor # is one less than this */
#ifdef DEV_MKNOD
#  define NUM_FO_DEVS (8)
#else
#  define NUM_FO_DEVS (255)
#endif /* DEV_MKNOD */
#define DEVNAME "fanout"
#define DEBUGLEVEL (2)


/* Data structure definitions */
/* This data structure describes one fanout device.  There
 * is one of these for each instance (minor #) of fanout */
struct fo {
	int minor;		/* minor number of this fanout instance */
	char *buf;		/* points to circular buffer, first char */
	int indx;		/* where to put next char received */
	loff_t count;		/* number chars received */
	wait_queue_head_t inq;	/* readers wait on this queue */
	struct semaphore sem;	/* lock to keep buf/indx sane */
#ifdef DEV_MKNOD
	struct device *dev;	/* automatic mknod */
#endif /* DEV_MKNOD */
};


/*  Function prototypes.  */
int fanout_init_module(void);
void fanout_exit_module(void);
static int fanout_open(struct inode *, struct file *);
static int fanout_release(struct inode *, struct file *);
static ssize_t fanout_read(struct file *, char *, size_t, loff_t *);
static ssize_t fanout_write(struct file *, const char *, size_t, loff_t *);
static unsigned int fanout_poll(struct file *, poll_table *);


/* Global variables */
static int buffersize = 0x4000;		/* Circular buffer size 0x4000 (16K) */
static unsigned int numberofdevs = NUM_FO_DEVS;
static int fo_major = 0;		/* major device number */
/* debuglevel controls whether a printk is executed
 * 0 = no printk at all
 * 1 = printk on error only
 * 2 = printk on errors and on init/remove
 * 3 = debug printk to trace calls into fanout
 * 4 = debug trace inside of fanout calls 
 */
static unsigned int debuglevel = DEBUGLEVEL;	/* printk verbosity */

struct cdev fo_cdev;		/* a char device global just 1 */
dev_t fo_devicenumber;		/* first device number */

#ifdef DEV_MKNOD
static struct class *fo_class;		/* fanout class */
static mode_t nodemode = 0666;		/* special files permissions bits */
					/* PARAM_DESC uses that value */
/* forward declaration */
static char *fo_dev_devnode(struct device *dev, umode_t *mode);
#endif /* DEV_MKNOD */

module_param(buffersize, int, S_IRUSR);
module_param(debuglevel, int, S_IRUSR);
module_param(numberofdevs, int, S_IRUSR);
#ifdef DEV_MKNOD
module_param(nodemode, int, S_IRUSR);
#endif /* DEV_MKNOD */

static struct fo *fo_devs;	/* point to devices (minors) */


/* map the callbacks into this driver */
static struct file_operations fanout_fops = {
	.owner = THIS_MODULE,
	.read = fanout_read,
	.open = fanout_open,
	.write = fanout_write,
	.poll = fanout_poll,
	.release = fanout_release
};


/* Module description and macros */
MODULE_DESCRIPTION
	("A device to replicate input (writer) on all outputs (readers), readers block, writer never blocks");
MODULE_AUTHOR("Bob Smith");
MODULE_LICENSE("GPL");
MODULE_PARM_DESC(buffersize, "Size of each buffer. default=16384 (16K) ");
MODULE_PARM_DESC(debuglevel, "Debug level. Higher=verbose. default=2");
MODULE_PARM_DESC(numberofdevs,
		 "Create this many minor devices. default=16");
#ifdef DEV_MKNOD
MODULE_PARM_DESC(nodemode, "Special files permission bits. default=0666");
#endif /* DEV_MKNOD */


int fanout_init_module(void)
{
	int i, err;
	fo_devs = kmalloc(numberofdevs * sizeof(struct fo), GFP_KERNEL);
	if (fo_devs == NULL) {
		if (debuglevel >= 1)
			printk(KERN_ALERT "%s: init fails. no memory.\n",
					DEVNAME);
		return 0;
	}
	/* clean memory and init device structures */
	memset(fo_devs, 0, numberofdevs * sizeof(struct fo));
	for (i = 0; i < numberofdevs; i++) {	/* for every minor device */
		fo_devs[i].minor = i;		/* set number */
		fo_devs[i].buf = (char *) 0;	/* init buf */
		fo_devs[i].indx = 0;		/* init index */
		fo_devs[i].count = 0;		/* init count */
		init_waitqueue_head(&fo_devs[i].inq);
#ifdef init_MUTEX
		init_MUTEX(&fo_devs[i].sem);	/* init sema */
#else
		sema_init(&fo_devs[i].sem,1);	/* init sema */
#endif

	}

	err = alloc_chrdev_region(&fo_devicenumber, 0, numberofdevs, DEVNAME);
	if (err < 0) {
		if (debuglevel >= 1)
			printk(KERN_ALERT "%s: init fails. err=%d.\n",
				DEVNAME, err);
		return err;
	}
	fo_major = MAJOR(fo_devicenumber);	/* save assign major */
	cdev_init(&fo_cdev, &fanout_fops);	/* init dev structures */
	kobject_set_name(&(fo_cdev.kobj), "%s%d", DEVNAME, fo_devicenumber);

#ifdef DEV_MKNOD
	fo_class = class_create(THIS_MODULE, DEVNAME);
	if (IS_ERR(fo_class)) {
		if (debuglevel >= 1)
			printk(KERN_ALERT "%s: class_create fails.\n", DEVNAME);
		cdev_del(&fo_cdev);		/* delete major device */
		kfree(fo_devs);			/* free */
		fo_devs = NULL;			/* reset pointer */
		return PTR_ERR(fo_class);
	}

	/* limit permission bits */
	nodemode &= 0666;
	/* callback to set permission bits for the nodes we will create */
	fo_class->devnode = fo_dev_devnode;
#endif /* DEV_MKNOD */

	err = cdev_add(&fo_cdev, fo_devicenumber, numberofdevs);
	if (err < 0) {
		if (debuglevel >= 1)
			printk(KERN_ALERT "%s: init fails. err=%d.\n",
					DEVNAME, err);
		return err;
	}

#ifdef DEV_MKNOD
	/* Create the special files and register with sysfs */
	for (i = 0; i < numberofdevs; i++) {	/* for every minor device */
		fo_devs[i].dev = device_create(fo_class, NULL,
			MKDEV(fo_major, i), NULL, i == 0 ? "%s" : "%s%d",
			DEVNAME, i);
		if (IS_ERR(fo_devs[i].dev)) {
			if (debuglevel >= 1)
			 	printk(KERN_ALERT \
					"%s%d: device_create fails. err=%ld.\n",
					DEVNAME, i, PTR_ERR(fo_devs[i].dev));
		}

	}
#endif /* DEV_MKNOD */

	if (debuglevel >= 2) {
		printk(KERN_INFO
			"%s: Installed %d minor devices on major number %d.\n",
		   			DEVNAME, numberofdevs, fo_major);
	}
	return 0;			/* success */
}


void fanout_exit_module(void)
{
	int i;

	if (!fo_devs)		/* anything to release ? */
		return;

	for (i = 0; i < numberofdevs; i++) {	/* for every minor */

#ifdef DEV_MKNOD
		/* Delete the special file for this minor */
		device_destroy(fo_class, MKDEV(fo_major, i));
#endif /* DEV_MKNOD */

		if (fo_devs[i].buf)
			kfree(fo_devs[i].buf);	/* free alloced memory */
	}

	cdev_del(&fo_cdev);		/* delete major device */
	kfree(fo_devs);			/* free */
	fo_devs = NULL;			/* reset pointer */

#ifdef DEV_MKNOD
	class_destroy(fo_class);
#endif /* DEV_MKNOD */

	unregister_chrdev_region(fo_devicenumber, numberofdevs);

	if (debuglevel >= 2)
		printk(KERN_INFO "%s: Uninstalled.\n", DEVNAME);
}


static int fanout_open(struct inode *inode, struct file *filp)
{
	int mnr = iminor(inode);
	struct fo *dev = &fo_devs[mnr];
	if (debuglevel >= 3)
		printk(KERN_DEBUG "%s open. Minor#=%d\n", DEVNAME, mnr);

	if (down_interruptible(&dev->sem))	/* prevent races on open */
		return -ERESTARTSYS;

	if (!dev->buf) {
		/* alloc the buffer, shared by all readers */
		dev->buf = kmalloc(buffersize, GFP_KERNEL);
		if (!dev->buf) {
			if (debuglevel >= 1) {
				printk(KERN_ALERT "%s: No memory dev=%d.\n",
						DEVNAME, mnr);
			}
			up(&dev->sem);	/* unlock sema */
			return -ENOMEM;
		}
	}

	/* store which fanout device in the file's private data */
	filp->private_data = (void *) dev;

	/* define the file to be immediately caught up with the fanout dev */
	filp->f_pos = dev->count;
	up(&dev->sem);		/* unlock semaphore we are done */
	return nonseekable_open(inode, filp);	/* success */
}


static int fanout_release(struct inode *inode, struct file *filp)
{
	if (debuglevel >= 3)
		printk(KERN_DEBUG "%s close. Minor#=%d.\n", DEVNAME,
			((struct fo *) filp->private_data)->minor);

	return 0;			/* success */
}


static ssize_t fanout_read(struct file *filp, char __user * buff,
			   size_t count, loff_t * offset)
{
	int ret;
	loff_t xfer;		/* num bytes read from fanout buf */
	int cpcnt, cpstrt;	/* cp count and start location */
	struct fo *dev = (struct fo *) filp->private_data;

	if (down_interruptible(&dev->sem))	/* lock semaphore */
		return -ERESTARTSYS;

	if (debuglevel >= 3)
		printk(KERN_DEBUG "%s: read %zu char from dev%d, off=%lld.\n",
		   DEVNAME, count, dev->minor, *offset);

	/* Wait here until new data is available */
	while (*offset == dev->count) {
		up(&dev->sem);		/* unlock sema */
		if (wait_event_interruptible(dev->inq, (*offset != dev->count)))
			return -ERESTARTSYS;
		if (down_interruptible(&dev->sem))	/* lock */
			return -ERESTARTSYS;
	}

	/* Verify that data requested is in the buffer or is next byte */
	xfer = dev->count - *offset;	/* send count minus requested pointer */
	if ((xfer > (loff_t) buffersize) || (xfer < 0)) {
		printk(KERN_DEBUG "%s: Overrun. xfer=%lld buffersize=%d",
				 DEVNAME, xfer, buffersize);
		up(&dev->sem);		/* unlock sema */
		return -EPIPE;		/* buffer overrun */
	}

	/* Copy the new data out to the user */
	xfer = dev->count - *offset;	/* amount of data available to copy */

	/* BUG: we need to check for a wrap on offset and count */

	 /* xfer less then available when requested */
	xfer = ((loff_t)count < xfer) ? (loff_t)count : xfer;
	ret = xfer;			/* we will handle these bytes */
	while (xfer) {
		/* copy start is where the reader last read (indx - (count - offset)) */
		cpstrt = dev->indx - (dev->count - *offset);
		if (cpstrt < 0) {	/* adjust copy count if needed */
			cpcnt = ((loff_t)(-cpstrt) <  xfer) ? (loff_t)(-cpstrt) : xfer;
			cpstrt += buffersize;
		} else {
			cpcnt = xfer;
		}

		if (copy_to_user(buff, dev->buf + cpstrt, cpcnt)) {
			up(&dev->sem);
			return -EFAULT;
		}

		buff += cpcnt;
		xfer -= cpcnt;
		*offset += cpcnt;
	}

	up(&dev->sem);		/* unlock sema */

	return ret;
}


static ssize_t fanout_write(
	struct file *filp,
	const char __user * buff,
	size_t count, loff_t * off)
{
	struct fo *dev = filp->private_data;

	int ret;
	int xfer;			/* num bytes to read from user */
	int cpcnt;		/* num bytes in a copy */

	if (down_interruptible(&dev->sem)) {	/* lock semaphore */
		return -ERESTARTSYS;
	}

	if (debuglevel >= 3)
		printk(KERN_DEBUG "%s: write %zu char to dev%d, off=%d.\n",
		   DEVNAME, count, dev->minor, (int) *off);

	/* Copy at most one-quarter of the circular buffer size.  This
	 * gives readers more of a chance to wake up and get some data 
	 * In other words feed the reader little chuncks of data, they will
	 * call again if they still want more
	 */
	ret = xfer = min((int) count, buffersize / 4);

	/* loop over the amount since the buffer is not a single block
	 * but wraps arround
	 */
	while (xfer) {
		cpcnt = buffersize - dev->indx;
		cpcnt = min(cpcnt, xfer);

		if (debuglevel >= 3)
			printk(KERN_DEBUG "%s: write copy from user(%p,%p,%d)\n",
		   	DEVNAME, dev->buf + dev->indx, buff, cpcnt);

		if (copy_from_user(dev->buf + dev->indx, buff, cpcnt)) {
			up(&dev->sem);	/* unlock semaphore */
			return -EFAULT;
		}
		*off += cpcnt;
		dev->indx += cpcnt;
		dev->indx = (dev->indx == buffersize) ? 0 : dev->indx;
		xfer -= cpcnt;	
		buff += cpcnt;
	}

	dev->count += ret;		/* update file size */
	up(&dev->sem);			/* unlock semaphore */

	/* This is what the readers have been waiting for */
	wake_up_interruptible(&dev->inq);

	return ret;
}

static unsigned int fanout_poll(struct file *filp, poll_table * ppt)
{
	/* The circular buffer is always available for writing */
	int ready_mask = POLLOUT | POLLWRNORM;

	struct fo *dev = filp->private_data;
	poll_wait(filp, &dev->inq, ppt);

	if (filp->f_pos != dev->count) {
		ready_mask = (POLLIN | POLLRDNORM);
	}

	if (debuglevel >= 3)
		printk(KERN_DEBUG "%s: poll returns 0x%x\n",
					DEVNAME, ready_mask);

	return ready_mask;
}

#ifdef DEV_MKNOD
/* callback invoked when making the nodes */
static char *fo_dev_devnode(struct device *dev, umode_t *mode)
{
	if (!mode)
		return NULL;
	if (MAJOR(dev->devt) == fo_major)
		*mode = nodemode;
	return kasprintf(GFP_KERNEL, "%s", dev_name(dev));
}
#endif /* DEV_MKNOD */

module_init(fanout_init_module);
module_exit(fanout_exit_module);
