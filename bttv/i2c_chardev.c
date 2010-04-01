/*
 * 2.1.x only
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/locks.h>
#include <linux/sched.h>
#include <linux/malloc.h>
#include <asm/uaccess.h>

#include "i2c.h"

/* ------------------------------------------------------------------------ */
/* FIXME: stick this into a header file                                     */

/* ioctls */
#define I2C_SLAVE       0x0706
#define I2C_WRITE_SIZE  0x0712
#define I2C_WRITE_BUF   0x0713
#define I2C_RESET       0x07fd

#define I2C_BUFFER_SIZE 64       /* buffer size for write b4 read        */

/* ----------------------------------------------------------------------- */
/* this allows i2c bus access from userspace
 * 
 * taken from the i2c package:
 *     available from http://www.tk.uni-linz.ac.at/~simon/private/i2c/
 *     written by Simon G. Vogl <simon@tk.uni-linz.ac.at>
 *
 * interface is binary compatible, the detect utility from the i2c package
 * finds all the chips on my hauppauge card, and eeprom tool reads happily
 * the content of the eeprom chip.
 *
 * Not all ioctl's implemented yet, only the most important stuff is in now.
 * Note: address handling (I2C_SLAVE ioctl) is different here:
 *       7bit address+direction bit  instead of  8bit "write address" 
 *
 */

#define I2C_MAJOR       89                       /* Device major number  */

#define MAX_BUSSES      8
static struct i2c_bus *busses[MAX_BUSSES];

/* 
 * private information for each open file:
 */ 
struct i2c_data {
        int address;            /* address slave chip                   */
        int writelength;        /* bytes to write before read           */
        char buf[I2C_BUFFER_SIZE];/* write buffer                       */
};


static long long i2c_dev_lseek(struct file * file, long long offset,
			       int origin) 
{
	return -ESPIPE;	
}


static int i2c_dev_open(struct inode * inode, struct file * file) 
{
	unsigned int minor = MINOR(inode->i_rdev);
	struct i2c_data *data;

	if (!busses[minor])
		return -ENODEV;	

	if (NULL == (data = kmalloc(sizeof(struct i2c_data),GFP_KERNEL)))
		return -ENOMEM;

	data->address	= 0x00;
	file->private_data = data;

	MOD_INC_USE_COUNT;
	return 0;
}

static int i2c_dev_release(struct inode * inode, struct file * file) 
{
	struct i2c_data *data;

	data=(struct i2c_data *)file->private_data;
	kfree(data);
	file->private_data=NULL;

	MOD_DEC_USE_COUNT;
	return 0;
}

static ssize_t i2c_dev_write(struct file * file, const char * buf,
			     size_t count, loff_t *ppos)
{
	struct inode *inode = file->f_dentry->d_inode;
	unsigned int minor  = MINOR(inode->i_rdev);
	const char *ptr = buf;
	struct i2c_data *data;
	int c,ret = 0;
	unsigned long flags;

	if (!busses[minor])
		return -EINVAL;
	data=(struct i2c_data *)file->private_data;

	LOCK_I2C_BUS(busses[minor]);
	i2c_start(busses[minor]);
	if (0 != i2c_sendbyte(busses[minor], data->address << 1, 0))
		goto ioerr;
	while (count > 0) {
		if (get_user(c,ptr))
			goto fault;
		if (0 != i2c_sendbyte(busses[minor],c,0))
			goto ioerr;
		ptr++;
		ret++;
		count--;
	}
	i2c_stop(busses[minor]);
	UNLOCK_I2C_BUS(busses[minor]);

	return ret;

ioerr:
	i2c_stop(busses[minor]);
	UNLOCK_I2C_BUS(busses[minor]);
	return -EREMOTEIO;

fault:
	i2c_stop(busses[minor]);
	UNLOCK_I2C_BUS(busses[minor]);
	return -EFAULT;
}

static ssize_t i2c_dev_read(struct file * file,char * buf,
			    size_t count, loff_t *ppos) 
{
	struct inode *inode = file->f_dentry->d_inode;
	unsigned int minor = MINOR(inode->i_rdev);
	char *ptr = buf;
	struct i2c_data *data;
	int i,val,ret = 0;
	unsigned long flags;

	if (!busses[minor])
		return -EINVAL;
	data=(struct i2c_data *)file->private_data;
	
	LOCK_I2C_BUS(busses[minor]);
	if (data->writelength > 0) {
		i2c_start(busses[minor]);
		if (0 != i2c_sendbyte(busses[minor], data->address << 1, 0))
			goto ioerr;
		for (i = 0; i < data->writelength; i++) {
			if (0 != i2c_sendbyte(busses[minor],data->buf[i],0))
				goto ioerr;
		}
	}
	i2c_start(busses[minor]);
	if (0 != i2c_sendbyte(busses[minor], data->address << 1 | 0x01, 0))
		goto ioerr;
	while (count > 0) {
		val = i2c_readbyte(busses[minor], (count == 1) ? 1 : 0);
		if (put_user((char)(val),ptr))
			goto fault;
		ptr++;
		ret++;
		count--;		
	}
	i2c_stop(busses[minor]);
	UNLOCK_I2C_BUS(busses[minor]);
	
	return ret;

ioerr:
	i2c_stop(busses[minor]);
	UNLOCK_I2C_BUS(busses[minor]);
	return -EREMOTEIO;

fault:
	i2c_stop(busses[minor]);
	UNLOCK_I2C_BUS(busses[minor]);
	return -EFAULT;
}

static int i2c_dev_ioctl(struct inode *inode, struct file *file,
			 unsigned int cmd, unsigned long arg)
{
	unsigned int minor = MINOR(inode->i_rdev);
	struct i2c_data *data;
	int ret = 0;
	unsigned long flags;

	if (!busses[minor])
		return -EINVAL;
	data=(struct i2c_data*)file->private_data;

	switch (cmd) {
#if 0
	case I2C_RETRIES:
		i2c_table[minor].retries = arg;
		break;
#endif
	case I2C_SLAVE:
		data->address = arg;
		break;
	case I2C_WRITE_SIZE:
		if (arg >= I2C_BUFFER_SIZE)
			return -E2BIG;
		data->writelength = arg;
		break;
	case I2C_WRITE_BUF:
		copy_from_user(data->buf,(char*)arg,data->writelength);
		break;
	case I2C_RESET:
		LOCK_I2C_BUS(busses[minor]);
		i2c_reset(busses[minor]);
		UNLOCK_I2C_BUS(busses[minor]);
		break;
	default:
		ret = -EINVAL;
	}
	return ret;
}

static struct file_operations i2c_fops = {
	i2c_dev_lseek,
	i2c_dev_read,
	i2c_dev_write,
	NULL,  			/* i2c_readdir	*/
	NULL,			/* i2c_select 	*/
	i2c_dev_ioctl,
	NULL,			/* i2c_mmap 	*/
	i2c_dev_open,
	i2c_dev_release,
};

/* ----------------------------------------------------------------------- */

static int chardev_attach(struct i2c_device *device)
{
	int i;

	for (i = 0; i < MAX_BUSSES; i++)
		if (NULL == busses[i])
			break;
	if (i == MAX_BUSSES)
		return -1;
	busses[i] = device->bus;
	strcpy(device->name,"chardev");
	printk("i2c_chardev: attached bus %s to minor %d\n",busses[i]->name,i);

	return 0;
}

static int chardev_detach(struct i2c_device *device)
{
	int i;

	for (i = 0; i < MAX_BUSSES; i++)
		if (device->bus == busses[i])
			break;
	if (i == MAX_BUSSES)
		return -1;
	printk("i2c_chardev: detached bus %s from minor %d\n",busses[i]->name,i);
	busses[i] = NULL;

	return 0;
}

struct i2c_driver i2c_driver_chardev = {
    "chardev",                    /* name       */
    I2C_DRIVERID_CHARDEV,         /* ID         */
    0, 0xfe,                      /* addr range */

    chardev_attach,
    chardev_detach,
    NULL
};

/* ----------------------------------------------------------------------- */

#ifdef MODULE

int init_module(void)
{
	if (register_chrdev(I2C_MAJOR,"i2c",&i2c_fops)) {
		printk("i2c: unable to get major %d\n", I2C_MAJOR);
		return -EBUSY;
	}
	i2c_register_driver(&i2c_driver_chardev);
	return 0;
}

void cleanup_module(void)
{
	i2c_unregister_driver(&i2c_driver_chardev);
	unregister_chrdev(I2C_MAJOR,"i2c");
}

#endif

