/* ------------------------------------------------------------------------- */
/* i2c.c - a device driver for the iic-bus interface			     */
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
#define RCSID "$Id: i2c.c,v 1.1 1998/05/25 12:08:09 i2c Exp i2c $"
/* ------------------------------------------------------------------------- */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/malloc.h>

#include "i2c.h"
 
/* ----- global defines ---------------------------------------------------- */
#define DEB(x)		/* should be reasonable open, close &c. 	*/
#define DEB2(x) 	/* low level debugging - very slow 		*/
#define DEBE(x)	x	/* error messages 				*/
#define DEBI(x) 	/* ioctl and its arguments 			*/

/* exclusive access to the bus */
#define I2C_LOCK(adap) spin_lock_irqsave(&adap->lock,adap->lockflags)
#define I2C_UNLOCK(adap) spin_unlock_irqrestore(&adap->lock,adap->lockflags)

/* ----- global variables -------------------------------------------------- */

/**** algorithm list */
struct i2c_algorithm *algorithms[I2C_ALGO_MAX];
static int algo_count;

/**** algorithm list */
struct i2c_adapter *adapters[I2C_ADAP_MAX];
static int adap_count;

/**** drivers list */
struct i2c_driver *drivers[I2C_DRIVER_MAX];
static int driver_count;

/**** clients list */
struct i2c_client *clients[I2C_CLIENT_MAX];
static int client_count;

/* ---------------------------------------------------    
 * registering functions 
 * --------------------------------------------------- 
 */

/* -----
 * Algorithms - used to access groups of similar hw adapters or
 * specific interfaces like the PCF8584 et al.
 */
int i2c_register_algorithm(struct i2c_algorithm *algo)
{
	int i;

	for (i = 0; i < I2C_ALGO_MAX; i++)
		if (NULL == algorithms[i])
			break;
	if (I2C_ALGO_MAX == i)
		return -ENOMEM;

	algorithms[i] = algo;
	algo_count++;
	MOD_INC_USE_COUNT;
	DEB(printk("i2c: algorithm %s registered.\n",algo->name));
	return 0;	
}


int i2c_unregister_algorithm(struct i2c_algorithm *algo)
{
	int i;

	for (i = 0; i < I2C_ALGO_MAX; i++)
		if (algo == algorithms[i])
			break;
	if (I2C_ALGO_MAX == i) 
	{
		printk(KERN_WARNING "i2c: unregister_algorithm algo not found: %s\n",
			algo->name);
		return -ENODEV;
	}

	algorithms[i] = NULL;
	algo_count--;
	MOD_DEC_USE_COUNT;
	DEB(printk("i2c: algorithm unregistered: %s\n",algo->name));
	return 0;    
}


/* -----
 * i2c_register_adapter is called from within the algorithm layer,
 * when a new hw adapter registers. A new device is register to be
 * available for clients.
 */
int i2c_register_adapter(struct i2c_adapter *adap)
{
	int i;

	for (i = 0; i < I2C_ADAP_MAX; i++)
		if (NULL == adapters[i])
			break;
	if (I2C_ADAP_MAX == i)
		return -ENOMEM;

	adapters[i] = adap;
	adap_count++;
/*	MOD_INC_USE_COUNT;*/

	/* init data types */

	adap->lock = (spinlock_t)SPIN_LOCK_UNLOCKED;


	/* inform drivers of new adapters */
	for (i=0;i<I2C_DRIVER_MAX;i++)
		if (drivers[i]!=NULL && drivers[i]->flags&DF_NOTIFY)
			drivers[i]->attach_adapter(adap);

	DEB(printk("i2c: adapter %s registered.\n",adap->name));
	return 0;	
}

int i2c_unregister_adapter(struct i2c_adapter *adap)
{
	int i;

	for (i = 0; i < I2C_ADAP_MAX; i++)
		if (adap == adapters[i])
			break;
	if (I2C_ADAP_MAX == i) 
	{
		printk(KERN_WARNING "i2c: unregister_adapter adap not found: %s\n",
			adap->name);
		return -ENODEV;
	}

	adapters[i] = NULL;
	adap_count--;

/*	MOD_DEC_USE_COUNT;*/

	/* inform drivers */
	for (i=0;i<I2C_DRIVER_MAX;i++)
		if (drivers[i]!=NULL && drivers[i]->flags&DF_NOTIFY)
			drivers[i]->detach_adapter(adap);

	DEB(printk("i2c: adapter unregistered: %s\n",adap->name));
	return 0;    
}

/* -----
 * What follows is the "upwards" interface: commands for talking to clients,
 * which implement the functions to access the physical information of the
 * chips.
 */

int i2c_register_driver(struct i2c_driver *driver)
{
	int i;

	for (i = 0; i < I2C_DRIVER_MAX; i++)
		if (NULL == drivers[i])
			break;
	if (I2C_DRIVER_MAX == i)
		return -ENOMEM;

	drivers[i] = driver;
	driver_count++;
	MOD_INC_USE_COUNT;

	if ( driver->flags&DF_NOTIFY )
	for (i=0;i<I2C_ADAP_MAX;i++)
		if (adapters[i]!=NULL)
			driver->attach_adapter(adapters[i]);

	DEB(printk("i2c: driver %s registered.\n",driver->name));
	return 0;
}

int i2c_unregister_driver(struct i2c_driver *driver)
{
	int i;

	for (i = 0; i < I2C_DRIVER_MAX; i++)
		if (driver == drivers[i])
			break;
	if (I2C_DRIVER_MAX == i) 
	{
		printk(KERN_WARNING "i2c: unregister_driver %s not found\n",
			driver->name);
		return -ENODEV;
	}

	drivers[i] = NULL;
	driver_count--;
	MOD_DEC_USE_COUNT;
	DEB(printk("i2c: driver unregistered: %s\n",driver->name));
	return 0;
}


int i2c_attach_client(struct i2c_client *client)
{
	struct i2c_adapter *adapter = client->adapter;
	struct i2c_algorithm *algo  = adapter->algo;
	int i;

	for (i = 0; i < I2C_CLIENT_MAX; i++)
		if (NULL == clients[i])
			break;
	if (I2C_CLIENT_MAX == i)
		return -ENOMEM;

	clients[i] = client;
	client_count++;
	if (algo->client_register != NULL) 
		algo->client_register(client);
	DEB(printk("i2c: client %s registered.\n",client->name));
	return 0;
}


int i2c_detach_client(struct i2c_client *client)
{
	struct i2c_adapter *adapter = client->adapter;
	struct i2c_algorithm *algo  = adapter->algo;
	int i;

	for (i = 0; i < I2C_CLIENT_MAX; i++)
		if (client == clients[i])
			break;
	if (I2C_CLIENT_MAX == i) 
	{
		printk(KERN_WARNING "i2c: unregister_client %s not found\n",
			client->name);
		return -ENODEV;
	}

	clients[i] = NULL;
	client_count--;
	if (algo->client_unregister != NULL) 
		algo->client_unregister(client);
	DEB(printk("i2c: client unregistered: %s\n",client->name));
	return 0;    
}

int i2c_init(void)
{
	/* clear algorithms */
	memset(algorithms,0,sizeof(algorithms));
	memset(adapters,0,sizeof(adapters));
	memset(clients,0,sizeof(clients));
	memset(drivers,0,sizeof(drivers));
	algo_count=0;
	adap_count=0;
	client_count=0;
	driver_count=0;
	
	printk("i2c module initialized.\n");
	return 0;
}


/* ----------------------------------------------------
 * the functional interface to the i2c busses.
 * ----------------------------------------------------
 */

int i2c_probe(struct i2c_adapter *adap, int low_addr, int hi_addr)
{
	struct i2c_client client;
	int i;

	memset(&client,0,sizeof(client));
	client.adapter=adap;
	I2C_LOCK(adap);
	for (i = low_addr; i <= hi_addr; i++) {
		client.addr=i;
		if (0 == adap->algo->master_send(&client,NULL,0))
			break;
	}
	I2C_UNLOCK(adap);
	return (i <= hi_addr) ? i : -1;
}

int i2c_master_send(struct i2c_client *client,const char *buf ,int count)
{
	int ret;
	struct i2c_adapter *adap=client->adapter;
	DEB(printk("master_send: writing %d bytes on %s.\n",
		count,client->adapter->name));

	I2C_LOCK(adap);
	ret = adap->algo->master_send(client,buf,count);
	I2C_UNLOCK(adap);

	return ret;
}

int i2c_master_recv(struct i2c_client *client, char *buf ,int count)
{
	int ret;
	struct i2c_adapter *adap=client->adapter;
	DEB(printk("master_recv: reading %d bytes on %s.\n",
		count,client->adapter->name));

	I2C_LOCK(adap);
	ret = adap->algo->master_recv(client,buf,count);
	I2C_UNLOCK(adap);
	DEB2(printk("master_recv: read %d bytes\n",ret));
	return ret;
}

int i2c_master_comb(struct i2c_client *client, char *readbuf,const char *writebuf, 
	int nread, int nwrite, int flags)
{
	int ret;
	struct i2c_adapter *adap=client->adapter;
	DEB(printk("master_comb: %s - %d in %d out on %s.\n",
		flags?"rd,wr":"wr,rd",nread,nwrite,
		client->adapter->name));

	I2C_LOCK(adap);
	ret = adap->algo->master_comb(client,readbuf,writebuf,
		nread,nwrite,flags);
	I2C_UNLOCK(adap);

	return ret;
}


int i2c_control(struct i2c_client *client,
	unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	struct i2c_adapter *adap = client->adapter;

	DEBI(printk("i2c ioctl, cmd: 0x%x, arg: %#lx\n", cmd, arg));
	switch ( cmd ) {
		case I2C_RETRIES:
			adap->retries = arg;
			break;
		case I2C_TIMEOUT:
			adap->timeout = arg;
			break;
		default:
			if (adap->algo->algo_control!=NULL)
				ret = adap->algo->algo_control(adap,cmd,arg);
	}
	return ret;
}


#ifdef MODULE
/*
EXPORT_SYMBOL(i2c_register_algorithm);
EXPORT_SYMBOL(i2c_unregister_algorithm);
*/

int init_module(void) 
{
	return i2c_init();
}

void cleanup_module(void) 
{
}
#endif
