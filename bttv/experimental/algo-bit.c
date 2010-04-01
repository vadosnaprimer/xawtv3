/* ------------------------------------------------------------------------- */
/* adap-bit.c i2c driver algorithms for bit-shift adapters		     */
/* ------------------------------------------------------------------------- */
/*   Copyright (C) 1995-97 Simon G. Vogl

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.		     */
/* ------------------------------------------------------------------------- */
static char alg_rcsid[] = "$Id: algo-bit.c,v 1.1 1998/05/25 12:08:00 i2c Exp i2c $";

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/malloc.h>
#include <linux/version.h>


#if LINUX_VERSION_CODE >= 0x020100
#  include <asm/uaccess.h>
#else
#  include <asm/segment.h>
#endif


#include <linux/ioport.h>
#include <linux/errno.h>
#include <linux/sched.h>

#include "i2c.h"
#include "algo-bit.h"

/* ----- global defines ---------------------------------------------------- */
#define DEB(x) x	/* should be reasonable open, close &c. 	*/
#define DEB2(x) 	/* low level debugging - very slow 		*/
#define DEBE(x)  	/* error messages 				*/
#define DEBI(x) 	/* ioctl and its arguments 			*/
#define DEBACK(x) x 	/* ack failed message				*/
#define DEBSTAT(x) 	/* print several statistical values		*/

#define DEBPROTO(x) x  	/* debug the protocol by showing transferred bytes*/

/* debugging - slow down transfer to have a look at the data .. 	*/
/* I use this with two leds&resistors, each one connected to sda,scl 	*/
/* respectively. This makes sure that the algorithm works. Some chips   */
/* might not like this, as they have an internal timeout of some mils	*/
/*
#define SLO_IO      jif=jiffies;while(jiffies<=jif+i2c_table[minor].veryslow)\
			if (need_resched) schedule();
*/

/* ----- global variables ---------------------------------------------	*/

#ifdef SLO_IO
	int jif;
#endif


static int test=1;	/* see if the line-setting functions work	*/
static int scan=0;	/* have a look at what's hanging 'round		*/
/*
 *  This array contains the hw-specific functions for
 *  each port (hardware) type.
 */
static struct bit_adapter *bit_adaps[BIT_ADAP_MAX];
static int adap_count;
static struct i2c_adapter *i2c_adaps[BIT_ADAP_MAX];

/* --- setting states on the bus with the right timing: ---------------	*/

#define setsda(adap,val) adap->setsda(adap->data, val)
#define setscl(adap,val) adap->setscl(adap->data, val)
#define getsda(adap) adap->getsda(adap->data)
#define getscl(adap) adap->getscl(adap->data)

static inline void sdalo(struct bit_adapter *adap)
{
    setsda(adap,0);
    udelay(adap->udelay);
}

static inline void sdahi(struct bit_adapter *adap)
{
    setsda(adap,1);
    udelay(adap->udelay);
}

static inline void scllo(struct bit_adapter *adap)
{
    setscl(adap,0);
    udelay(adap->udelay);
#ifdef SLO_IO
    SLO_IO
#endif
}

/*
 * Raise scl line, and do checking for delays. This is necessary for slower
 * devices.
 */
static inline int sclhi(struct bit_adapter *adap)
{
	int start=jiffies;

	setscl(adap,1);

	udelay(adap->udelay);
 	while (! getscl(adap) ) {		/* wait till high	*/
		setscl(adap,1);
		if (start+adap->timeout <= jiffies) {
/*			DEBE(printk("i2c(bit): (%s) sclhi timed out after %d jiffies\n", 
				 adap->name, adap->timeout ) );
*/			return -ETIMEDOUT;
		}
		if (need_resched)
			schedule();
	}
	DEBSTAT(printk("needed %ld jiffies\n", jiffies-start));
#ifdef SLO_IO
	SLO_IO
#endif
	return 0;
} 


/* --- other auxiliary functions --------------------------------------	*/
static void i2c_start(struct bit_adapter *adap) 
{
	/* assert: scl, sda are high */
	DEBPROTO(printk("S "));
	sdalo(adap);
	scllo(adap);
}

static void i2c_repstart(struct bit_adapter *adap) 
{
	/* scl, sda may not be high */
	DEBPROTO(printk(" Sr "));
	setsda(adap,1);
	setscl(adap,1);
	udelay(adap->udelay);
	
	sdalo(adap);   /* includes delay! */
	scllo(adap);
}


static void i2c_stop(struct bit_adapter *adap) 
{
	DEBPROTO(printk("P\n"));
	/* assert: scl is low */
	sdalo(adap);
	sclhi(adap); 
	sdahi(adap);
}

/* send a byte without start cond., look for arbitration, 
   check ackn. from slave */
/* return 1 if ok */
static int i2c_outb(struct bit_adapter *adap, char c)
{
	int i;
	int sb;
	int ack;

	/* assert: scl is low */
	DEB2(printk(" i2c_outb:%2.2X\n",c&0xff));
	for ( i=7 ; i>=0 ; i-- ) {
		sb = c & ( 1 << i );
		setsda(adap,sb);
		udelay(adap->udelay);
		DEBPROTO(printk("%d",sb!=0));
		if (sclhi(adap)<0) { /* timed out */
			sdahi(adap); /* we don't want to block the net */
			return -ETIMEDOUT;
		};
#if 0
		/* arbitrate here: */
		if ( sb && !getsda(adap) ){
			/* we lost the arbitration process -> give up */
			DEBE(printk("i2c(bit): %s i2c_outb: arbitration bailout! \n", adap->name ));
			return -ETIMEDOUT;
		}
		/* scllo(adap); */
#endif
		setscl( adap, 0 );
		udelay(adap->udelay);
	}
	sdahi(adap);
	if (sclhi(adap)<0){ /* timeout */
		return -ETIMEDOUT;
	};
	/* read ack: SDA should be pulled down by slave */
	ack=getsda(adap);	/* ack: sda is pulled low ->success.	 */
	DEB2(printk(" i2c_outb: getsda() =  0x%2.2x\n", !ack ));

	DEBPROTO( printk("[%2.2x]",c&0xff) );
	DEBPROTO(if (0==ack) printk(" A "); else printk(" NA ") );
	scllo(adap);
	return 0==ack;		/* return 1 if device acked	 */
	/* assert: scl is low (sda undef) */
}



static int i2c_inb(struct bit_adapter *adap) 
{
	/* read byte via i2c port, without start/stop sequence	*/
	/* acknowledge is sent in i2c_read.			*/
	int i;
	char indata;

	/* assert: scl is low */
	DEB2(printk("i2c_inb.\n"));

	sdahi(adap);
	indata=0;
	for (i=0;i<8;i++) {
		if (sclhi(adap)<0) { /* timeout */
			return -ETIMEDOUT;
		};
		indata *= 2;
		if ( getsda(adap) ) 
		  indata |= 0x01;
		scllo(adap);
	}
	/* assert: scl is low */
    DEBPROTO(printk(" %2.2x", indata & 0xff));
    return (int) (indata & 0xff);
}


/* ----- level 2: communication with the kernel  ----- */
#if 0

static int i2c_write(struct inode * inode, struct file * file,
	const char * buf, int count)
{
	unsigned struct bit_adapter *adap = ADAP(inode->i_rdev);
	char c,adr;
	const char *temp = buf;
	int retval,i;
	int wrcount=0;
	struct i2c_data *data;

	data=(struct i2c_data *)file->private_data;
	if (data && (data->magic==I2C_MAGIC))
		adr = (data->address);
	else
		return -EINVAL;
	/* slave address is in the lower 7 bits -> shift left to make	*/
	/* space for the data direction bit 				*/
	adr <<= 1;

	DEB(printk("i2c%d: i2c_write: %d byte(s) to send\n", adap, count));

#if LINUX_VERSION_CODE >= 0x020100
	/* check data buffer once */
	if ( !access_ok(VERIFY_READ,buf,count)) {
		printk("i2c%d: Error accessing user memory!\n",adap);
		return -ENOMEM;
	}
#endif

	i2c_start(adap);
	i=0;
	while ( ! i2c_outb(adap,adr) ) {
		i2c_stop(adap);
		i++;
		i2c_start(adap);
		if (i>=i2c_table[adap].retries ) {
		  DEBACK(printk("i2c%d: i2c_write: address ack failed.\n",adap));
		  i2c_stop(adap);
		  return -EREMOTEIO;
		};
	}
	while (count > 0) {
#if LINUX_VERSION_CODE >= 0x020100
		__get_user(c,temp);	/* we use the non-checking version */
#else
		c = get_user(temp);
#endif
		DEB2(printk("i2c%d: i2c_write: writing %2.2X\n",adap, c&0xff));
		retval = i2c_outb(adap,c);
		if (retval>0) { 
			count--; 
			temp++;
			wrcount++;
		} else { /* arbitration or no acknowledge */
			DEBE(printk("i2c%d: i2c_write: error - bailout.\n",adap));
			i2c_stop(adap);
			return -EREMOTEIO; /* got a better one ?? */
		}
		/* /usr/src/linux/include/asm/delay.h */
		__delay(i2c_table[adap].mdelay * (loops_per_sec / 1000) );
	}
	i2c_stop(adap);
	DEB2(printk(" i2c_write: wrote %d bytes.\n",wrcount));
	return wrcount;
}


static int i2c_read(struct inode * inode, struct file * file,
	char * buf, int count) 
{
	unsigned struct bit_adapter *adap = ADAP(inode->i_rdev);
	char *temp = buf;
	char adr;
	int i,inval;
	int writetries=0;	/* try to write some rounds before giving up */
	int rdcount=0;   	/* counts bytes read */
	struct i2c_data *data;

	DEB(printk("i2c%d: ", adap ));
	DEB2(printk("i2c%d: i2c_read: %d byte(s) to read\n", adap, count));
	data=(struct i2c_data *)file->private_data;
	if (data && (data->magic==I2C_MAGIC))
		adr = (data->address);
	else
		return -EINVAL;
	/* slave address is in the lower 7 bits -> shift left to make	*/
	/* space for the data direction bit 				*/
	adr <<= 1;

#if LINUX_VERSION_CODE >= 0x020100
	/* check data buffer once */
	if ( !access_ok(VERIFY_WRITE,buf,count)) {
		printk("i2c%d: Error accessing user memory!\n",adap);
		return -ENOMEM;
	}
#endif

	i2c_start(adap);
	i=0;

newtry:

	if ( i2c_table[adap].flags & P_COMBI ) {
		/* This here is the repeated start condition stuff */
		int retval;
		/* for writing the subaddress, the R/W bit must be 0 */
		while (!i2c_outb(adap,adr)) { 	/* send address first */
			i2c_stop(adap);
			i++;
			if (i>=i2c_table[adap].retries )  {
				DEBACK(printk("i2c%d: i2c_read: address ack failed\n",adap));
				i2c_stop(adap);
				return -EREMOTEIO;
			};
			i2c_start(adap);  /* try again */
		}
		/* successful, reset fail counter */ 
		i = 0;
		/* send data */
		for (i=0;i<data->writelength;i++) {
			retval = i2c_outb(adap,data->buf[i]);
			if ( !retval ) {
				writetries++;
				if (writetries>=i2c_table[adap].retries ) {
					DEBE(printk("i2c%d: i2c_read: write data failed\n",adap));
					i2c_stop(adap);
					return -EREMOTEIO;
				};
				goto newtry;
			}
		}
		i2c_repstart(adap);
	}

	/* send address and do a normal read */ 
	if ( !i2c_outb(adap,adr | 0x01) ) {
		i2c_stop(adap);
		i++;
		if ( i>=i2c_table[adap].retries ) {
			DEBACK(printk("i2c%d: i2c_read: address ack failed.\n",adap));
			i2c_stop(adap);
			return -EREMOTEIO;
		};
		i2c_start(adap);
		goto newtry;
	}

	while (count > 0) {
		inval = i2c_inb(adap);
		if (inval>=0) {
			/* write result into user's buffer (macro) */
#if LINUX_VERSION_CODE >= 0x020100
			__put_user( (char)(inval) ,temp);
#else
		put_user((char)(inval),temp);
#endif
			rdcount++;
		}
		else {   /* read timed out */
			DEBE(printk("i2c%d: i2c_read: i2c_inb timed out.\n",adap));
			break;
		}

		if ( count > 1 ) {		/* send ack */
			sdalo(adap);
			DEBPROTO(printk(" Am "));
		} else {
			sdahi(adap);		/* neg. ack on last byte */
			DEBPROTO(printk(" NAm "));
		}
		if (sclhi(adap)<0) {		/* timeout */
			sdahi(adap);
			DEBE(printk("i2c%d: i2c_read: Timeout at ack\n", adap));
			return -ETIMEDOUT;
		};
		scllo(adap);
		sdahi(adap);
		temp++;
		count--;
	}

	i2c_stop(adap);
	DEB(printk("i2c%d: i2c_read: %d byte(s) read.\n", adap, rdcount ));
	return rdcount; 
}


static int i2c_ioctl(struct inode *inode, struct file *file,
	unsigned int cmd, unsigned long arg)
{
	unsigned struct bit_adapter *adap = ADAP(inode->i_rdev);
	int retval = 0;

	switch ( cmd ) {
		case I2C_TIMEOUT:
			i2c_table[adap].timeout = arg;
			break;
		case I2C_UDELAY:
			i2c_table[adap].udelay = arg;
			case I2C_MDELAY:
	 			i2c_table[adap].mdelay = arg;
			break;
		case I2C_RETRIES:
			i2c_table[adap].retries = arg;
			break;
#ifdef SLO_IO
		case I2C_V_SLOW: 
			i2c_table[adap].veryslow = arg;
			break;
#endif
		case I2C_RESET:
			setsda(adap,1);
			setscl(adap,1);
			break;
		default:
			retval = -EINVAL;
	}
	return retval;
}
#endif 

static int test_bus(struct bit_adapter *adap) {
	int scl,sda;
	scl=getscl(adap);
	sda=getsda(adap);
	printk("i2c(bit): Adapter: %s scl: %d  sda: %d -- testing...\n",
	adap->name,getscl(adap),getsda(adap));
	if (!scl || !sda ) {
		printk("i2c(bit): %s seems to be busy.\n",adap->name);
		goto bailout;
	}
	sdalo(adap);
	printk("i2c(bit):1 scl: %d  sda: %d \n",getscl(adap),getsda(adap));
	if ( 0 != getsda(adap) ) {
		printk("i2c(bit): %s SDA stuck high!\n",adap->name);
		sdahi(adap);
		goto bailout;
	}
	if ( 0 == getscl(adap) ) {
		printk("i2c(bit): %s SCL unexpected low while pulling SDA low!\n",
			adap->name);
		goto bailout;
	}		
	sdahi(adap);
	printk("i2c(bit):2 scl: %d  sda: %d \n",getscl(adap),getsda(adap));
	if ( 0 == getsda(adap) ) {
		printk("i2c(bit): %s SDA stuck low!\n",adap->name);
		sdahi(adap);
		goto bailout;
	}
	if ( 0 == getscl(adap) ) {
		printk("i2c(bit): %s SCL unexpected low while SDA high!\n",adap->name);
	goto bailout;
	}
	scllo(adap);
	printk("i2c(bit):3 scl: %d  sda: %d \n",getscl(adap),getsda(adap));
	if ( 0 != getscl(adap) ) {
		printk("i2c(bit): %s SCL stuck high!\n",adap->name);
		sclhi(adap);
		goto bailout;
	}
	if ( 0 == getsda(adap) ) {
		printk("i2c(bit): %s SDA unexpected low while pulling SCL low!\n",
			adap->name);
		goto bailout;
	}
	sclhi(adap);
	printk("i2c(bit):4 scl: %d  sda: %d \n",getscl(adap),getsda(adap));
	if ( 0 == getscl(adap) ) {
		printk("i2c(bit): %s SCL stuck low!\n",adap->name);
		sclhi(adap);
		goto bailout;
	}
	if ( 0 == getsda(adap) ) {
		printk("i2c(bit): %s SDA unexpected low while SCL high!\n",
			adap->name);
		goto bailout;
	}
	printk("i2c(bit): %s passed test.\n",adap->name);
	return 0;
bailout:
	return -ENODEV;
}

/* ----- Utility functions
 */

inline int try_address(struct bit_adapter *adap,unsigned char addr, int retries)
{
	int i,ret = -1;
	for (i=0;;) {
		ret = i2c_outb(adap,addr);
		if (ret==1)
			break;	/* success! */
		i2c_stop(adap);
		udelay(adap->udelay);
		if (++i >= retries)
			break;
		i2c_start(adap);
		udelay(adap->udelay);
	}
	DEB(if (i) printk("i2c(bit): needed %d retries for %d \n",i,addr));
	return ret;
}


static int bit_send(struct i2c_client *client,const char *buf, int count)
{
	struct i2c_adapter *adapter=client->adapter;
	struct bit_adapter *adap=(struct bit_adapter*)adapter->data;
	char c;
	const char *temp = buf;
	int ret,retval,i;
	int wrcount=0;
	unsigned int flags=client->flags;
	/* slave address is in the lower 7 bits -> shift left to make	*/
	/* space for the data direction bit 				*/

	DEB(printk("i2c(bit): %s i2c_write: %d byte(s) to send\n", adap->name, count));

	i2c_start(adap);
	i=0;

	/* first send address: */
	if ( flags & CF_TEN ) { 	/* a ten bit address 	*/
		unsigned char addr = 0xf0 | ((flags>>15)&0x06);
		printk("addr0: %d, sub %d\n",(unsigned char)addr,((flags>>15)&0x06));
		/* try extended address code...*/
		ret = try_address(adap, (unsigned char)addr, adapter->retries);
		if (ret!=1) {
			printk("died at extended address code.\n");
			return -EREMOTEIO;
		}
		/* the remaining 8 bit address */
		ret = i2c_outb(adap,client->addr);
		if (ret != 1) {
			printk("died at 2nd address code.\n");
			return -EREMOTEIO;
		}
		/* okay, now we are set up to send data*/
	} else {		/* normal 7bit address	*/
		char addr = ( client->addr << 1 );
		ret = try_address(adap, addr, adapter->retries);
		if (ret!=1)
			return -EREMOTEIO;
	}

	/* send the data */
	while (count > 0) {
		c = *temp;
		DEB2(printk("i2c(bit): %s i2c_write: writing %2.2X\n",adap->name, c&0xff));
		retval = i2c_outb(adap,c);
		if (retval>0) {
			count--; 
			temp++;
			wrcount++;
		} else { /* arbitration or no acknowledge */
			DEBE(printk("i2c(bit): %s i2c_write: error - bailout.\n",adap->name));
			i2c_stop(adap);
			return -EREMOTEIO; /* got a better one ?? */
		}
		/* from asm/delay.h */
		__delay(adap->mdelay * (loops_per_sec / 1000) );
	}
	i2c_stop(adap);
	DEB2(printk(" i2c_write: wrote %d bytes.\n",wrcount));
	return wrcount;
}


static int bit_recv(struct i2c_client *client,char *buf,int count)
{
	struct i2c_adapter *adapter = client->adapter;
	struct bit_adapter *adap = (struct bit_adapter*)adapter->data;
	char *temp = buf;
	unsigned int flags = client->flags;
	char addr;
	int ret=0,i,inval;
	int rdcount=0;   	/* counts bytes read */

	DEB2(printk("i2c(bit): %s i2c_read: %d byte(s) to read\n", adap->name, count));

	/* slave address is in the lower 7 bits -> shift left to make	*/
	/* space for the data direction bit 				*/
	
#if LINUX_VERSION_CODE >= 0x020100
	/* check data buffer once */
	if ( !access_ok(VERIFY_WRITE,buf,count)) {
		printk("i2c(bit): Error accessing user memory for bit_read!\n");
		return -ENOMEM;
	}
#endif
	i2c_start(adap);
	i=0;
	DEB2(printk("i2c(bit): flags %#x\n",flags));
	/* first send address: */
	if ( (flags & CF_TEN) != 0 ) { 	/* a ten bit address 	*/
		addr = 0xf0 | ((flags>>15)&0x06);
		printk("addr0: %d, sub %d\n",addr,((flags>>15)&0x06));
		/* try extended address code...*/
		ret = try_address(adap, addr, adapter->retries);
		if (ret!=1) {
			printk("died at extended address code.\n");
			return -EREMOTEIO;
		}
		/* the remaining 8 bit address */
		ret = i2c_outb(adap,client->addr);
		if (ret != 1) {
			printk("died at 2nd address code.\n");
			return -EREMOTEIO;
		}
		i2c_repstart(adap);
		/* okay, now switch into reading mode */
		addr |= 0x01;
		ret = try_address(adap, addr, adapter->retries);
		if (ret!=1) {
			printk("died at extended address code.\n");
			return -EREMOTEIO;
		}		
	} else {		/* normal 7bit address	*/
		addr = ( client->addr << 1 ) | 0x01;
		ret = try_address(adap, addr, adapter->retries);
		if (ret!=1)
			return -EREMOTEIO;
	}

	udelay(adap->udelay);
	/* now read the data */
	while (count > 0) {
		inval = i2c_inb(adap);
		if (inval>=0) {
			/* write result into user's buffer (macro) */
#if LINUX_VERSION_CODE >= 0x020100
			__put_user( (char)(inval) ,temp);
#else
		put_user((char)(inval),temp);
#endif
			rdcount++;
		}
		else {   /* read timed out */
			DEBE(printk("i2c(bit): i2c_read: i2c_inb timed out.\n"));
			break;
		}

		if ( count > 1 ) {		/* send ack */
			sdalo(adap);
			DEBPROTO(printk(" Am "));
		} else {
			sdahi(adap);		/* neg. ack on last byte */
			DEBPROTO(printk(" NAm "));
		}
		if (sclhi(adap)<0) {		/* timeout */
			sdahi(adap);
			DEBE(printk("i2c(bit): i2c_read: Timeout at ack\n"));
			return -ETIMEDOUT;
		};
		scllo(adap);
		sdahi(adap);
		temp++;
		count--;
	}

	i2c_stop(adap);
	DEB(printk("i2c(bit): i2c_read: %d byte(s) read.\n", rdcount ));
	return rdcount; 
}

static int bit_comb(struct i2c_client *client,char*r,const char*w,int i,int j, int k)
{
	return 0;
}


static int algo_control(struct i2c_adapter *adapter, 
	unsigned int cmd, unsigned long arg)
{
	return 0;
}

static int client_register(struct i2c_client *client)
{
	struct i2c_adapter *adapter = client->adapter;
	struct bit_adapter *adap = (struct bit_adapter*)adapter->data;

	if (adap->client_register != NULL)
		return adap->client_register(client);
	return 0;
}

int client_unregister(struct i2c_client *client)
{
	struct i2c_adapter *adapter = client->adapter;
	struct bit_adapter *adap = (struct bit_adapter*)adapter->data;

	if (adap->client_unregister != NULL)
		return adap->client_unregister(client);
	return 0;
}

/* -----exported algorithm data: -------------------------------------	*/

struct i2c_algorithm bit_algo = {
	"Bit-shift algorithm",
	ALGO_BIT,
	bit_send,			/* master_xmit		*/
	bit_recv,			/* master_recv		*/
	bit_comb,			/* master_comb		*/
	NULL,				/* slave_xmit		*/
	NULL,				/* slave_recv		*/
	algo_control,			/* ioctl		*/
	client_register,
	client_unregister,
};

/* 
 * registering functions to load algorithms at runtime 
 */
int i2c_bit_register_bus(struct bit_adapter *adap)
{
	int i,ack;
	struct i2c_adapter *i2c_adap;

	for (i = 0; i < BIT_ADAP_MAX; i++)
		if (NULL == bit_adaps[i])
			break;
	if (BIT_ADAP_MAX == i)
		return -ENOMEM;

	bit_adaps[i] = adap;
	adap_count++;
	DEB(printk("i2c(bit): algorithm %s registered.\n",adap->name));

	MOD_INC_USE_COUNT;

	if (test) {
		int ret = test_bus(adap);
		if (ret<0)
			return -ENODEV;
	}
	/* register new adapter to i2c module... */

	i2c_adap = kmalloc(sizeof(struct i2c_adapter), GFP_KERNEL);
	i2c_adap->name = adap->name;
	i2c_adap->id = bit_algo.id | adap->id;
	i2c_adap->algo = &bit_algo;
	i2c_adap->data = adap;
	i2c_adap->flags = 0;
	i2c_adap->timeout = 100;	/* default values, should	*/
	i2c_adap->retries = 3;		/* be replaced by defines	*/
	i2c_adaps[i] = i2c_adap;
	i2c_register_adapter(i2c_adap);

	/* scan bus */
	printk(KERN_INFO "i2c(bit): scanning bus %s.\n", adap->name);
	if (scan)
		for (i = 0x00; i < 0xff; i+=2) {
			i2c_start(adap);
			ack = i2c_outb(adap,i);
			i2c_stop(adap);
			if (ack>0) {
				printk(KERN_INFO 
				"i2c(bit):  found chip at addr=0x%2x\n",i);
			} 
		}
	return 0;
}


int i2c_bit_unregister_bus(struct bit_adapter *adap)
{
	int i;

	for (i = 0; i < BIT_ADAP_MAX; i++)
		if ( adap == bit_adaps[i])
			break;
	if ( BIT_ADAP_MAX == i) {
		printk(KERN_WARNING "i2c(bit): could not unregister bus: %s\n",
			adap->name);
		return -ENODEV;
	}

	MOD_DEC_USE_COUNT;
	
	bit_adaps[i] = NULL;
	i2c_unregister_adapter(i2c_adaps[i]);
	kfree(i2c_adaps[i]);
	i2c_adaps[i] = NULL;
	adap_count--;
	DEB(printk("i2c(bit): adapter unregistered: %s\n",adap->name));

	return 0;
}

int algo_bit_init (void)
{
	int i;

	for (i=0;i<BIT_ADAP_MAX;i++) {
		bit_adaps[i]=NULL;
	}
	adap_count=0;
	i2c_register_algorithm(&bit_algo);
	return 0;
}

#ifdef MODULE
MODULE_PARM(test, "i");
MODULE_PARM(scan, "i");

/*
EXPORT_SYMBOL(i2c_bit_register_bus);
EXPORT_SYMBOL(i2c_bit_unregister_bus);
*/

int init_module(void) 
{
	return algo_bit_init();
}

void cleanup_module(void) 
{
	i2c_unregister_algorithm(&bit_algo);
}
#endif
























