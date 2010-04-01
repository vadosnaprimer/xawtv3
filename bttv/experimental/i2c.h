/* ------------------------------------------------------------------------- */
/* 									     */
/* i2c.h - definitions for the \iic-bus interface			     */
/* 									     */
/* ------------------------------------------------------------------------- */
/*   Copyright (C) 1995 Simon G. Vogl

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
#ifndef _I2C_H
#define _I2C_H
#include <asm/spinlock.h>	/* for spinlock_t */

/* --- General options ------------------------------------------------	*/
#define I2C_MAJOR	89			/* Device major number	*/

#define I2C_ALGO_MAX	4
#define I2C_ADAP_MAX	16
#define I2C_DRIVER_MAX	16
#define I2C_CLIENT_MAX	32

struct i2c_algorithm;
struct i2c_adapter;
struct i2c_client;
struct i2c_driver;

/* ----- registration structures */
struct i2c_algorithm {
	char *name;				/* textual description 	*/
	unsigned int id;       
	int (*master_send)(struct i2c_client *,const char*,int);
	int (*master_recv)(struct i2c_client *,char*,int);
	int (*master_comb)(struct i2c_client *,char*,const char*,int,int,int);

	/* --- these optional/future use for some adapter types.*/
	int (*slave_send)(struct i2c_adapter *,char*,int);
	int (*slave_recv)(struct i2c_adapter *,char*,int);

	/* --- ioctl like call to set div. parameters. */
	int (*algo_control)(struct i2c_adapter *, unsigned int, unsigned long);

	/* --- administration stuff. */
	int (*client_register)(struct i2c_client *);
	int (*client_unregister)(struct i2c_client *);
};


/* i2c_adapter is the structure used to identify a physical i2c bus along
 * with the access algorithms necessary to access it.
 */
struct i2c_adapter {
	char *name;	/* some useful name to identify the adapter	*/
	unsigned int id;/* == is algo->id | hwdep.struct->id, 		*/
			/* for registered values see below		*/
	struct i2c_algorithm *algo;/* the algorithm to access the bus	*/

	void *data;	/* private data for the adapter			*/
			/* some data fields that are used by all types	*/
			/* these data fields are readonly to the public	*/
			/* and can be set via the i2c_ioctl call	*/

			/* data fields that are valid for all devices	*/
	spinlock_t lock;/* used to access the adapter exclusively	*/
	unsigned long lockflags;
	unsigned int flags;/* flags specifying div. data		*/
	int timeout;
	int retries;
};

/* i2c_client identifies a single device that is connected to an 
 * 	i2c bus.
 */
struct i2c_client {
	char *name;
	int id;
	unsigned int flags;		/* div., see below		*/
	unsigned char addr;		/* chip address - NOTE: 7bit 	*/
					/* addresses are stored in the	*/
					/* _LOWER_ 7 bits of this char	*/
					/* 10 bit addresses use the full*/
					/* 8 bits & set the correct	*/
					/* flags in the flags field...	*/
	struct i2c_adapter *adapter;	/* the adapter we sit on	*/
	struct i2c_driver *driver;	/* and our access routines	*/
	void *data;			/* for the clients		*/
};

/* a driver is capable of handling one or more physical devices present on
 * I2C adapters. This information is used to inform the driver of adapter
 * events.
 */

struct i2c_driver {
	char *name;
	int id;
	unsigned char  addr_l, addr_h;  /* address range of the chip 	*/
	unsigned int flags;		/* div., see below		*/

	int (*attach_adapter)(struct i2c_adapter *);
	int (*detach_adapter)(struct i2c_client *);
	int (*command)(struct i2c_client *client,unsigned int cmd, void *arg);

};

/*flags for the driver struct:
 */
#define DF_NOTIFY	0x01		/* notify on bus (de/a)ttaches 	*/
/* #define DF_SCAN		0x02	 scan the addr_l-_h for chips	*/

/*flags for the client struct:
 */
#define CF_TEN	0x100000	/* we have a ten bit chip address	*/
#define CF_TEN0	0x100000	/* herein lie the first 2 bits 		*/
#define CF_TEN1	0x110000
#define CF_TEN2	0x120000
#define CF_TEN3	0x130000
#define TENMASK	0x130000


/* ----- functions exported by i2c.o */

/* administration...
 */
extern int i2c_register_algorithm(struct i2c_algorithm *);
extern int i2c_unregister_algorithm(struct i2c_algorithm *);

extern int i2c_register_adapter(struct i2c_adapter *);
extern int i2c_unregister_adapter(struct i2c_adapter *);

extern int i2c_register_driver(struct i2c_driver *);
extern int i2c_unregister_driver(struct i2c_driver *);

extern int i2c_attach_client(struct i2c_client *);
extern int i2c_detach_client(struct i2c_client *);
extern int i2c_client_command(struct i2c_client *);

/* ----- ioctl like call to set div. parameters
 */
extern int i2c_control(struct i2c_client *,unsigned int, unsigned long);

/* ----- data transfer 
 */
extern int i2c_probe(struct i2c_adapter *adap, int low_addr, int hi_addr);

extern int i2c_master_send(struct i2c_client *,const char* ,int);
extern int i2c_master_recv(struct i2c_client *,char* ,int);
extern int i2c_master_comb(struct i2c_client *,char* ,const char* ,int, int, int);

/* --- flags for master_comb - combined read/write sequences w/ rep.start */
#define RD_AFTER_RW	0
#define WR_AFTER_RD	1

	/*--- these optional/future use for some adapter types.*/
extern int i2c_slave_send(struct i2c_client *,char*,int);
extern int i2c_slave_recv(struct i2c_client *,char*,int);


/* ----- commands for the ioctl like i2c_command call:
 * note that additional calls are defined in the algorithm and hw 
 *	dependent layers - these can be listed here, or see the 
 *	corresponding header files.
 */
				/* -> bit-adapter specific ioctls	*/
#define I2C_RETRIES	0x0701  /* number times a device adress should  */
				/* be polled when not acknowledging 	*/
#define I2C_TIMEOUT	0x0702	/* set timeout - call with int 		*/


/* this is for i2c-dev.c	*/
#define I2C_SLAVE	0x0703	/* Change slave address			*/
				/* Attn.: Slave address is 7 bits long, */
				/* 	these are to be passed as the	*/
				/*	lowest 7 bits in the arg.	*/
				/* for 10-bit addresses pass lower 8bits*/
#define I2C_TENBIT	0x0704	/* 	with 0-3 as arg to this call	*/
				/*	a value <0 resets to 7 bits	*/
/* ... algo-bit.c recognizes */
#define I2C_UDELAY	0x0705  /* set delay in microsecs between each  */
				/* written byte (except address)	*/
#define I2C_MDELAY	0x0706	/* millisec delay between written bytes */

#if 0
#define I2C_ADDR	0x0707	/* Change adapter's \iic address 	*/
				/* 	...not supported by all adap's	*/

#define I2C_RESET	0x07fd	/* reset adapter			*/
#define I2C_CLEAR	0x07fe	/* when lost, use to clear stale info	*/
#define I2C_V_SLOW	0x07ff  /* set jiffies delay call with int 	*/

#define I2C_INTR	0x0708	/* Pass interrupt number - 2be impl.	*/

#endif

/*
 * ---- Adapter types: Add a define statement & the struct ---------------
 *
 * First, we distinguish between several algorithms to access the hardware
 * interface types, as a PCF 8584 needs other care than a bit adapter.
 */

#define ALGO_NONE	0x000
#define ALGO_BIT	0x100	/* bit style adapters			*/
#define ALGO_PCF	0x200	/* PCF 8584 style adapters		*/

#define ALGO_MASK	0xf00	/* Mask for algorithms			*/
#define ALGO_SHIFT	0x08	/* right shift to get index values 	*/

#define I2C_HW_ADAPS	0x100	/* number of different hw implements per*/
				/* 	algorithm layer module		*/
#define I2C_HW_MASK	0xff	/* space for indiv. hw implmentations	*/


/* hw specific modules that are defined per algorithm layer
 */

/* --- Bit algorithm adapters 						*/
#define HW_B_LP		0x00	/* Parallel port Philips style adapter	*/
#define HW_B_LPC	0x01	/* Parallel port, over control reg.	*/
#define HW_B_SER	0x02	/* Serial line interface		*/
#define HW_B_ELV	0x03	/* ELV Card				*/
#define HW_B_VELLE	0x04	/* Vellemann K8000			*/
#define HW_B_BT848	0x05	/* BT848 video boards			*/

/* --- PCF 8584 based algorithms					*/
#define HW_P_LP		0x00	/* Parallel port interface		*/
#define HW_P_ISA	0x01	/* generic ISA Bus inteface card	*/
#define HW_P_ELEK	0x02	/* Elektor ISA Bus inteface card	*/

#define I2C_DRIVERID_MSP3400     1
#define I2C_DRIVERID_TUNER       2
#define I2C_DRIVERID_VIDEOTEXT   3


#endif
