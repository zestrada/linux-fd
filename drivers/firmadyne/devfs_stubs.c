#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/export.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/major.h>
#include <linux/miscdevice.h>
#include <linux/kernel.h>
#include <linux/uaccess.h>
#include <linux/proc_fs.h>
#include <linux/list.h>
#include <linux/slab.h>

#include "devfs_stubs.h"
#include "firmadyne.h"

#define STUB_ENTRIES \
	DEVICE(acos_nat_cli, 100, 0, open, read, write, close, acos_ioctl) \
	DEVICE(brcmboard, 206, 0, open, read, write, close, ioctl) \
	DEVICE(dsl_cpe_api, 107, 0, open, read, write, close, ioctl) \
	DEVICE(gpio, 252, 0, open, read, write, close, gpio_ioctl) \
	DEVICE(nvram, 111, 0, open, read, write, close, ioctl) \
	DEVICE(pib, 31, 3, open, read, write, close, ioctl) \
	DEVICE(sc_led, 225, 0, open, read, write, close, ioctl) \
	DEVICE(tca0, 183, 0, open, read, write, close, ioctl) \
	DEVICE(ticfg, 0, 0, open, read, write, close, ioctl) \
	DEVICE(watchdog, MISC_MAJOR, WATCHDOG_MINOR, open, read, write, close, ioctl) \
	DEVICE(wdt, 253, 0, open, read, write, close, ioctl) \
	DEVICE(zybtnio, 220, 0, open, read, write, close, ioctl)

static long acos_ioctl(struct file *file, unsigned int cmd, unsigned long arg_ptr) {
	int retval = 0;

	if (!devfs) {
		return -EINVAL;
	}

	printk(KERN_INFO MODULE_NAME": ACOS ioctl: 0x%x\n", cmd);

	switch (cmd) {
		// netgear R6XXX, R7XXX, R8XXX, etc...
		case 0x40046431:
		case 0x80046431:
			printk(KERN_WARNING MODULE_NAME": ACOS: agApi_GetFirstTriggerConf\n");
			retval = 1;
			break;
		case 0x40046432:
		case 0x80046432:
			printk(KERN_WARNING MODULE_NAME": ACOS: agApi_fwGetNextTriggerConf\n");
			retval = 1;
			break;
		default:
			retval = 0;
			break;
	}

	return retval;
}

static long ioctl(struct file *file, unsigned int cmd, unsigned long arg_ptr) {
	int retval = 0;

	if (!devfs) {
		return -EINVAL;
	}

	printk(KERN_INFO MODULE_NAME": ioctl: 0x%x\n", cmd);

	switch (cmd) {
		default:
			retval = 0;
			break;
	}

	return retval;
}

//If our process name is in /proc/firmadyne/gpio_fail, we'll fail
#include <linux/sched.h>
struct comm_list { 
    char comm[TASK_COMM_LEN+1]; 
    struct list_head list;
}; 

static LIST_HEAD(gpio_ioctl_fail_list);
static DEFINE_MUTEX(proc_gpio_ioctl_lock);

static long gpio_ioctl(struct file *file, unsigned int cmd, unsigned long arg_ptr) {
    int retval = 0;
    struct comm_list *comm_entry;

    list_for_each_entry(comm_entry, &gpio_ioctl_fail_list, list) {
        //Some process names are shorter, but we always have TASK_COMM_LEN bytes in our struct
        if(!strncmp(comm_entry->comm,current->comm,TASK_COMM_LEN)) {
            retval = -1;
            break;
        }
    } 

	printk(KERN_INFO MODULE_NAME": gpio_ioctl: 0x%x, ret: %d", cmd, retval);
    return retval;
}

static int open(struct inode *inode, struct file *file) {
/*
	if (inode->i_cdev != &c_dev) {
		return -ENODEV;
	}
*/
	if (!devfs) {
		return -EINVAL;
	}

	return 0;
}

static int close(struct inode *inode, struct file *file) {
	if (!devfs) {
		return -EINVAL;
	}

	return 0;
}

static ssize_t read(struct file *file, char __user *buf, size_t size, loff_t *offset) {
	const char data[] = "0";
	loff_t count = min((loff_t) size, ARRAY_SIZE(data) - *offset);

	if (!devfs) {
		return -EINVAL;
	}

	if (*offset >= ARRAY_SIZE(data)) {
		return 0;
	}

	if (copy_to_user(buf, data + *offset, count)) {
		return -EFAULT;
        }

	*offset += count;
	return count;
}

static ssize_t write(struct file *file, const char __user *buf, size_t size, loff_t *offset) {
	if (!devfs) {
		return -EINVAL;
	}

	return size;
}

static int acl(struct device *dev, struct kobj_uevent_env *env) {
	add_uevent_var(env, "DEVMODE=%#o", 0666);
	return 0;
}

#define DEVICE(a, b, c, d, e, f, g, h) \
	static dev_t a##_devno = MKDEV(b, c); \
	static struct cdev a##_cdev; \
	static struct class *a##_class; \
	static struct device *a##_dev; \
	static struct file_operations a##_fops = { \
		.owner		= THIS_MODULE, \
		.open		= d, \
		.read		= e, \
		.write		= f, \
		.release	= g, \
		.unlocked_ioctl	= h, \
	};

	STUB_ENTRIES
#undef DEVICE

static struct proc_dir_entry *fd_procdir; //Could move to firmadyne.h
static struct proc_dir_entry *gpio_ioctl_procfile;

static char proc_buf[TASK_COMM_LEN+1];

//For now, we require separate writes for each process name
static ssize_t proc_gpio_ioctl_write(struct file *file, const char __user *ubuf, size_t count, loff_t *ppos) {
    struct comm_list *comm_entry, *new_entry; 

    mutex_lock(&proc_gpio_ioctl_lock);

    //TODO: prepend write with '-' to remove 
    if(count > TASK_COMM_LEN)
        count = TASK_COMM_LEN;

    copy_from_user(proc_buf, ubuf, count);
    proc_buf[strcspn(proc_buf, "\n")] = '\0'; //strip newline if there is one

    list_for_each_entry(comm_entry, &gpio_ioctl_fail_list, list) {
        //Some process names are shorter, but we always have TASK_COMM_LEN bytes in our struct
        if(!strncmp(comm_entry->comm,proc_buf,count)) {
            goto proc_write_out; //entry exists, bail
        }
    } 

    new_entry = kmalloc(sizeof(struct comm_list), GFP_KERNEL);
    strncpy(new_entry->comm, proc_buf, count);
    new_entry->comm[TASK_COMM_LEN] = '\0';
    list_add(&new_entry->list, &gpio_ioctl_fail_list);

proc_write_out:
    mutex_unlock(&proc_gpio_ioctl_lock);
    *ppos = count;
    return count;
}

static ssize_t proc_gpio_ioctl_read(struct file *file, char __user *ubuf,size_t count, loff_t *ppos) {
    struct comm_list *comm_entry; 
    ssize_t bytes_read=0;
    char *output_buf;

    //TODO: support partial reads
    if(*ppos)
        return 0;
    
    output_buf = kmalloc(count, GFP_KERNEL);
    if(!output_buf)
        return -1;

    mutex_lock(&proc_gpio_ioctl_lock);
    list_for_each_entry(comm_entry, &gpio_ioctl_fail_list, list) {
        strcpy(output_buf+bytes_read, comm_entry->comm); 
        bytes_read+=strlen(comm_entry->comm);
        if(bytes_read<count)
            output_buf[bytes_read++] = '\n';//replace terminator with newline
    } 
    mutex_unlock(&proc_gpio_ioctl_lock);

    if(bytes_read <= count)  {
        copy_to_user(ubuf, output_buf, bytes_read);
    } else {
        bytes_read = -1;//We don't support partial reads
    }

    *ppos=bytes_read;
    kfree(output_buf);
    return bytes_read;
}
static struct proc_dir_entry *gpio_ioctl_procfile;

static struct file_operations gpio_ioctl_fops = 
{
    .owner = THIS_MODULE,
    .read = proc_gpio_ioctl_read,
    .write = proc_gpio_ioctl_write,
};

int register_devfs_stubs(void) {
	int ret = 0;

	if (!devfs) {
		return ret;
	}

#define DEVICE(a, b, c, d, e, f, g, h) \
	if ((ret = register_chrdev_region(a##_devno, 1, #a)) < 0) { \
		printk(KERN_WARNING MODULE_NAME": Cannot register character device: %s, 0x%x, 0x%x!\n", #a, MAJOR(a##_devno), MINOR(a##_devno)); \
		goto a##_out; \
	} \
\
	if (IS_ERR(a##_class = class_create(THIS_MODULE, #a))) { \
		printk(KERN_WARNING MODULE_NAME": Cannot create device class: %s!\n", #a); \
		unregister_chrdev_region(a##_devno, 1); \
		ret = PTR_ERR(a##_class); \
		goto a##_out; \
	} \
	a##_class->dev_uevent = acl; \
\
	cdev_init(&a##_cdev, &a##_fops); \
\
	if ((ret = cdev_add(&a##_cdev, a##_devno, 1)) < 0) { \
		printk(KERN_WARNING MODULE_NAME": Cannot add class device: %s!\n", #a); \
		class_destroy(a##_class); \
		unregister_chrdev_region(a##_devno, 1); \
		goto a##_out; \
	} \
\
	if (IS_ERR(a##_dev = device_create(a##_class, NULL, a##_devno, NULL, #a))) { \
		printk(KERN_WARNING MODULE_NAME": Cannot create device: %s!\n", #a); \
		cdev_del(&a##_cdev); \
		class_destroy(a##_class); \
		unregister_chrdev_region(a##_devno, 1); \
		ret = PTR_ERR(a##_dev); \
	} \
a##_out:

	STUB_ENTRIES
#undef DEVICE

      
    //procfs initialization
    //some of this should move to firmadyne.c if we want to stick with this
    fd_procdir = proc_mkdir(MODULE_NAME,NULL);
    if( !fd_procdir ) {
        printk(KERN_WARNING MODULE_NAME": Couldn't create /proc/%s!\n", 
               MODULE_NAME);
        return -ENOMEM;
    }

    gpio_ioctl_procfile = proc_create("gpio_ioctl",0666,fd_procdir,&gpio_ioctl_fops);
    if( !gpio_ioctl_procfile ) {
        printk(KERN_WARNING MODULE_NAME": Couldn't create /proc/%s/%s!\n", 
               MODULE_NAME, "gpio_ioctl");
        return -ENOMEM;
    }

    
	return ret;
}

void unregister_devfs_stubs(void) {
	if (!devfs) {
		return;
	}

#define DEVICE(a, b, c, d, e, f, g, h) \
	device_destroy(a##_class, a##_devno); \
	cdev_del(&a##_cdev); \
	class_destroy(a##_class); \
	unregister_chrdev_region(a##_devno, 1);

	STUB_ENTRIES
#undef DEVICE
    remove_proc_entry(MODULE_NAME,NULL);
    //TODO: tear down gpio_ioctl list with list_for_each_entry_safe and kfree
}
