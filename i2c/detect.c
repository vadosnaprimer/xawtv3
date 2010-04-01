/* ------------------------------------------------------------------------- */
/* detect -- look who's there. Gets address acks from all devices on the bus.*/
/*		It should not change any values in the peripherals.	     */
/* ------------------------------------------------------------------------- */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <linux/i2c.h>


/* some common i2c chip addresses on bt848 grabber boards */
static struct BTTV_LIST {
    int   addr;
    char *name;
} known[] = {
    { 0x22, "saa5249 videotext" },
    { 0x80, "msp34xx sound processor" },
    { 0x82, "TDA8425 audio chip" },
    { 0xa0, "eeprom (Hauppauge boards)" },
    { 0xb6, "TDA9850 audio chip" },
    { 0xc0, "tuner" },
    { 0xc2, "tuner" },
    { 0xee, "eeprom (STB boards)" },
    { 0, NULL }
};

int
main(int argc, char *argv[])
{
    int i,j,f,c;
    char b[40];
    char *device = "/dev/i2c-0";
	
    /* parse options */
    while (-1 != (c=getopt(argc,argv,"hd:"))) {
	switch (c){
	case 'd':
	    if (optarg)
		device = optarg;
	    break;
	case 'h':
	default:
	    printf("This tool tries to detect devices on the i2c-bus\n");
	    printf("usage: %s [ -d device ]\n",argv[0]);
	    exit(1);
	}
    }

    if (-1 == (f = open(device,O_RDWR))) {
	fprintf(stderr,"open %s: %s\n",device,strerror(errno));
	exit(1);
    }
    for (i=0; i < 256; i += 2){
	ioctl(f,I2C_SLAVE,i>>1);
	fprintf(stderr,"0x%x\r",i);
	if (-1 != read(f,b,0)) {
	    printf("0x%x: ",i);
	    for (j = 0; known[j].name != NULL; j++) {
		if (known[j].addr == i) {
		    printf("%s\n",known[j].name);
		    break;
		}
	    }
	    if (known[j].name == NULL)
		printf("???\n");
	}
    }
    close(f);
    exit(0);
}
