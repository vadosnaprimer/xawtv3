#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/malloc.h>

#include "i2c.h"
#include "videodev.h"

#include "tuner.h"

int debug = 0; /* insmod parameter */
int type  = 0; /* tuner type */

#define dprintk     if (debug) printk

#if LINUX_VERSION_CODE > 0x020100
MODULE_PARM(debug,"i");
MODULE_PARM(type,"i");
#endif

struct tuner {
	int type;            /* chip type */
	int freq;            /* keep track of the current settings */
	int radio;
};

static struct i2c_driver driver;
static struct i2c_client client_template;

/* ---------------------------------------------------------------------- */

struct tunertype {
  char *name;
  unchar Vendor;
  unchar Type;
  
  ushort thresh1; /* frequency Range for UHF,VHF-L, VHF_H */   
  ushort thresh2;  
  unchar VHF_L;
  unchar VHF_H;
  unchar UHF;
  unchar config; 
  unchar I2C;
  ushort IFPCoff;
};

/*
 *	The floats in the tuner struct are computed at compile time
 *	by gcc and cast back to integers. Thus we don't violate the
 *	"no float in kernel" rule.
 */
static struct tunertype tuners[] = {
        {"Temic PAL", TEMIC, PAL,
                16*140.25,16*463.25,0x02,0x04,0x01,0x8e,0xc2,623},
	{"Philips PAL_I", Philips, PAL_I,
	        16*140.25,16*463.25,0xa0,0x90,0x30,0x8e,0xc0,623},
	{"Philips NTSC", Philips, NTSC,
	        16*157.25,16*451.25,0xA0,0x90,0x30,0x8e,0xc0,732},
	{"Philips SECAM", Philips, SECAM,
	        16*168.25,16*447.25,0xA7,0x97,0x37,0x8e,0xc0,623},
	{"NoTuner", NoTuner, NOTUNER,
	         0        ,0        ,0x00,0x00,0x00,0x00,0x00,000},
	{"Philips PAL", Philips, PAL,
	        16*168.25,16*447.25,0xA0,0x90,0x30,0x8e,0xc0,623},
	{"Temic NTSC", TEMIC, NTSC,
	        16*157.25,16*463.25,0x02,0x04,0x01,0x8e,0xc2,732},
	{"TEMIC PAL_I", TEMIC, PAL_I,
	        16*170.00,16*450.00,0xa0,0x90,0x30,0x8e,0xc2,623},
};

/* ---------------------------------------------------------------------- */

static int tuner_getstatus (struct tuner *t)
{
#if 0
	return i2c_read(t->bus,t->addr+1);
#endif
}

#define TUNER_POR       0x80
#define TUNER_FL        0x40
#define TUNER_AFC       0x07

static int tuner_islocked (struct tuner *t)
{
        return (tuner_getstatus (t) & TUNER_FL);
}

static int tuner_afcstatus (struct tuner *t)
{
        return (tuner_getstatus (t) & TUNER_AFC) - 2;
}

static void set_tv_freq(struct i2c_client *client, int freq)
{
	struct tuner     *t   = (struct tuner*)client->data;
	struct tunertype *tun = &tuners[t->type];
	unsigned char buffer[4];
	int rc;
	u8 config;
	u16 div;

	if (freq < tun->thresh1) 
		config = tun->VHF_L;
	else if (freq < tun->thresh2) 
		config = tun->VHF_H;
	else
		config = tun->UHF;

	div=freq + (int)(16*38.9);
  	div&=0x7fff;

	buffer[0] = (div>>8) & 0x7f;
	buffer[1] = div      & 0xff;
	buffer[2] = tun->config;
	buffer[3] = config;
	if (4 != (rc = i2c_master_send(client,buffer,4)))
		printk("tuner: i2c i/o error: rc == %d (should be 4)\n",rc);
}

static void set_radio_freq(struct i2c_client *client, int freq)
{
	struct tuner     *t   = (struct tuner*)client->data;
	struct tunertype *tun = &tuners[t->type];
	unsigned char buffer[4];
	int rc;
	u8 config;
	u16 div;

	config = 0xa5;
	div=freq + (int)(16*10.7);
  	div&=0x7fff;

	buffer[0] = (div>>8) & 0x7f;
	buffer[1] = div      & 0xff;
	buffer[2] = tun->config;
	buffer[3] = config;
	if (4 != (rc = i2c_master_send(client,buffer,4)))
		printk("tuner: i2c i/o error: rc == %d (should be 4)\n",rc);
}

/* ---------------------------------------------------------------------- */

static int
tuner_attach(struct i2c_adapter *adap)
{
	struct tuner      *t;
	struct i2c_client *client;
	int addr;

	/* check bus */
	addr = i2c_probe(adap, 0xc0>>1, 0xde>>1);
	if (addr == -1)
		return -ENODEV;
	printk("tuner: chip found @ 0x%x\n",addr);
	
	client = (struct i2c_client *)kmalloc(sizeof(struct i2c_client),
					      GFP_KERNEL);
        if (client==NULL)
                return -ENOMEM;
	memcpy(client,&client_template,sizeof(struct i2c_client));
	client->addr = addr;
	client->adapter = adap;
	client->data = t = kmalloc(sizeof(struct tuner),GFP_KERNEL);
	if (NULL == t) {
		kfree(client);
                return -ENOMEM;
	}
	memset(t,0,sizeof(struct tuner));
	t->type = type;
	i2c_attach_client(client);
	return 0;
}

static int
tuner_detach(struct i2c_client *client)
{
	struct tuner *t = (struct tuner*)client->data;

	i2c_detach_client(client);
	kfree(t);
	kfree(client);
	return 0;
}

static int
tuner_command(struct i2c_client *client, unsigned int cmd, void *arg)
{
	struct tuner *t    = (struct tuner*)client->data;
	int          *iarg = (int*)arg;
	
	switch (cmd) {
	case TUNER_SET_TYPE:
		t->type = *iarg;
		dprintk("tuner: type set to %d (%s)\n",
			t->type,tuners[t->type].name);
		break;
		
	case TUNER_SET_TVFREQ:
		dprintk("tuner: tv freq set to %d.%02d\n",
			(*iarg)/16,(*iarg)%16*100/16);
		set_tv_freq(client,*iarg);
		t->radio = 0;
		t->freq = *iarg;
		break;
		
	case TUNER_SET_RADIOFREQ:
		dprintk("tuner: radio freq set to %d.%02d\n",
			(*iarg)/16,(*iarg)%16*100/16);
		set_radio_freq(client,*iarg);
		t->radio = 1;
		t->freq = *iarg;
		break;
		
	default:
		return -EINVAL;
	}
	return 0;
}

/* ----------------------------------------------------------------------- */

static struct i2c_driver driver = {
	"i2c tv tuner driver",
	-1,
	0xc0,0xde,
	DF_NOTIFY,
	tuner_attach,
	tuner_detach,
	tuner_command,
};

static struct i2c_client client_template = 
{
	"i2c tv tuner chip",          /* name       */
	I2C_DRIVERID_TUNER,           /* ID         */
	0,
	0,
	NULL,
	&driver
};

#ifdef MODULE
int init_module(void)
#else
int tuner_init(void)
#endif
{
	i2c_register_driver(&driver);
	return 0;
}

#ifdef MODULE
void cleanup_module(void)
{
	i2c_unregister_driver(&driver);
}
#endif

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
