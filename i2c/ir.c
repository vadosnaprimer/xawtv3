/*
 * programm for the ir control connected
 * to the svhs input on the hauppauge wintv
 * card (87X chip)
 *
 * (c) 1999 Mathias Kuester <mkuester@fh-stralsund.de>
 *
 * Version 0.01 18.05.1999
 *
 * before use : 
 *                           mknod /dev/ir 127 0
 *
 * client for xawtv & kde at : 
 *                           http://www.user.fh-stralsund.de/~mkuester
 *
 * how can i use this programm ?:
 *
 * 1) open /dev/ir
 * 2) read 2 !!! bytes
 * 3) check returned size when 0 no key pressed or error
 * 4) check the first byte when 0 no key pressed
 * 5) when != 0 check the second byte as (unsigned char) for the key
 *
 * KEYTABLE:
 *
 * 	TV		60
 * 	CH+		128
 * 	RADIO		48
 * 	VOL-		68
 * 	FULL SCREEN	184
 * 	VOL+		64
 * 	MUTE		52
 * 	CH-		132
 * 	SOURCE		136
 * 	1		4
 * 	2		8
 * 	3		12
 * 	4		16
 * 	5		20
 * 	6		24
 * 	7		28
 * 	8		32
 * 	9		36
 * 	RESERVED	120
 * 	0		0
 * 	MINIMIZE	152
 *  
 */


#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/malloc.h>
#include <linux/fs.h>
#include <linux/wrapper.h>

#include "i2c.h"

struct ir
{
	struct i2c_bus   *bus;     /* where is our chip */
	int               addr;
};

#define dprintk     if (debug) printk

#if LINUX_VERSION_CODE > 0x020100
MODULE_PARM(debug,"i");
MODULE_PARM(type,"i");
#endif

#define I2C_INFRAROT	0x34

struct ir *mk_ir = NULL;

static int ir_read (struct inode * node, struct file * filep, char * buf, int count)
{

 unsigned char *tmp = buf, a;
 
 if ( count < 2 )
     return 0;

 i2c_start(mk_ir->bus);
 if ( 0 != i2c_sendbyte(mk_ir->bus,0x34,2000) ||
      0 != i2c_sendbyte(mk_ir->bus,0,0) ||
      0 != i2c_sendbyte(mk_ir->bus,mk_ir->addr>>8  ,0) ||
      0 != i2c_sendbyte(mk_ir->bus,mk_ir->addr&0xff,0) ) {
	return 0;
 } else {
    i2c_stop(mk_ir->bus);
    udelay(1000*2);
    i2c_start(mk_ir->bus);
    if (0 != i2c_sendbyte(mk_ir->bus,0x34+1,2000) )
	return 0;
    else {
	/* wenn Taste gedrueckt > 0 */
	a = i2c_readbyte(mk_ir->bus,0);
	put_user(a,(tmp++));
	/* Tastencode */
	a = i2c_readbyte(mk_ir->bus,1);
	put_user(a,(tmp++));
    }
 }

 i2c_stop(mk_ir->bus);

 /* read to clear LED ??? */
 
 a = i2c_read(mk_ir->bus,mk_ir->addr);
 a = i2c_read(mk_ir->bus,mk_ir->addr);
 a = i2c_read(mk_ir->bus,mk_ir->addr);
 
 return(2);
}

static int ir_open ( struct inode * i, struct file * f )
{
 return 0;
}

static void ir_close (struct inode * i, struct file * f )
{
 return;
}

/* ---------------------------------------------------------------------- */

static struct file_operations ir = {
    NULL,
    ir_read,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    ir_open,
    ir_close,
    NULL
};

/* ---------------------------------------------------------------------- */

static int ir_attach(struct i2c_device *device)
{
	struct ir *t;
	int n;

	if ( mk_ir != NULL ) {
		printk("infrarot: sorry\n");
		return -1;
	}
	
	if ( (n=register_chrdev(127,"ir",&ir)) < 0 ) {
		printk("unable to get major (register_chrdev)\n");
		return -1;
	}
	
	if(device->bus->id!=I2C_BUSID_BT848)
		return -EINVAL;
		
	device->data = t = kmalloc(sizeof(struct ir),GFP_KERNEL);
	if (NULL == t)
		return -ENOMEM;

	memset(t,0,sizeof(struct ir));
	strcpy(device->name,"infrarot");
	t->bus  = device->bus;
	t->addr = device->addr;
	
	mk_ir = t;

	MOD_INC_USE_COUNT;
	return 0;

}

static int ir_detach(struct i2c_device *device)
{
	struct ir *t = (struct ir*)device->data;
	kfree(t);
	MOD_DEC_USE_COUNT;
	unregister_chrdev(127,"ir");
	return 0;
}

/* ----------------------------------------------------------------------- */

static int ir_command(struct i2c_device *device,
	      unsigned int cmd, void *arg)
{
	return 0;
}

/* ----------------------------------------------------------------------- */

struct i2c_driver i2c_driver_ir = 
{
	"infrarot",                      /* name       */
	4,           			 /* ID         */
	I2C_INFRAROT, I2C_INFRAROT,      /* addr range */

	ir_attach,
	ir_detach,
	ir_command
};

#ifdef MODULE
int init_module(void)
#else
int i2c_ir_init(void)
#endif
{
	i2c_register_driver(&i2c_driver_ir);
	return 0;
}

#ifdef MODULE
void cleanup_module(void)
{
	i2c_unregister_driver(&i2c_driver_ir);
}
#endif
