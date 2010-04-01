#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <linux/i2c.h>

char *dev="/dev/i2c-0";
int  wr=0, x=0, adr=0xa0;

/* ---------------------------------------------------------------------- */

void usage(char *name)
{
    fprintf(stderr,"This is a i2c EEPROM tool\n");
    fprintf(stderr,"  read  data: %s [ options ] > file\n",name);
    fprintf(stderr,"  write data: %s [ options ] -w < file\n",name);
    fprintf(stderr,"\n");
    fprintf(stderr,"File format is a hex dump.  Without \"-w\" switch\n");
    fprintf(stderr,"only parsing (and printing) of the data is done,\n");
    fprintf(stderr,"writing is skipped.\n");
    fprintf(stderr,"\n");
    fprintf(stderr,"options:\n");
    fprintf(stderr,"  -d device  device to use         [%s]\n",dev);
    fprintf(stderr,"  -a addr    set slave address     [0x%02x]\n",adr);
    fprintf(stderr,"  -x         print some hauppauge data decoded\n");
    fprintf(stderr,"\n");
    exit(1);
}

/* ---------------------------------------------------------------------- */
/* Hauppauge boards */

#define VENDOR_AVER_MEDIA 0x1461
#define VENDOR_HAUPPAUGE  0x0070
#define VENDOR_FLYVIDEO   0x1851
#define VENDOR_STB        0x10B4

#define STR(array,i) (i < sizeof(array)/sizeof(char*) ? array[i] : "unknown")

static char *chiptype[] = {
	"Reserved", "BT815", "BT817", "BT819", "BT815A", "BT817A", "BT819A",
	"BT827", "BT829", "BT848", "BT848A", "BT849A", "BT829A", "BT827A",
	"BT878", "BT879", "BT880"
};

static char *sndtype[] = {
	"None", "TEA6300", "TEA6320", "TDA9850", "MSP3400C", "MSP3410D",
	"MSP3415", "MSP3430"};

static char *sndout[] = {
	"None", "Internal", "BackPanel", "Internal and BackPanel"};

static char *teletext[] = { "None", "SAA5246", "SAA5284", "Yes (Software)"};

static char *h_tuner [] = {
    "",
    "External",
    "Unspecified",
    "Phillips FI1216",
    "Phillips FI1216MF",
    "Phillips FI1236",
    "Phillips FI1246",
    "Phillips FI1256",
    "Phillips FI1216 MK2",
    "Phillips FI1216MF MK2",
    "Phillips FI1236 MK2",
    "Phillips FI1246 MK2",
    "Phillips FI1256 MK2",
    "Temic 4032FY5",
    "Temic 4002FH5",
    "Temic 4062FY5",
    "Phillips FR1216 MK2",
    "Phillips FR1216MF MK2",
    "Phillips FR1236 MK2",
    "Phillips FR1246 MK2",
    "Phillips FR1256 MK2",
    "Phillips FM1216",
    "Phillips FM1216MF",
    "Phillips FM1236",
};

void dump_hauppauge(unsigned char *tvee)
{
    unsigned int model;
    unsigned int revision;
    unsigned int serial;
    unsigned int vendor_id;
    unsigned int id;
    int i, b1, b2, b3;
    
    id = (tvee[252] << 8) | tvee[253];
    vendor_id = (tvee[254] << 8) | tvee[255];
    printf("Card Vendor: 0x%04x, Model: 0x%04x\n", vendor_id, id);
    if (1 /* vendor_id == VENDOR_HAUPPAUGE */) {
	b1 = 2 + tvee[1];
	b2 = 2 + tvee[b1 + 2];
	b3 = 2 + tvee[b1 + 2 + b2 + 1];		/* Blocklängen */
	model = (tvee[12] << 8 | tvee[11]);
	revision = (tvee[15] << 16 | tvee[14] << 8 | tvee[13]);
	serial = (tvee[b1 + 12] << 16 | tvee[b1 + 11] << 8 | tvee[b1 + 10]);
	printf("Hauppauge Model %d Rev. %c%c%c%c\n",
	       model,
	       ((revision >> 18) & 0x3f) + 32,
	       ((revision >> 12) & 0x3f) + 32,
	       ((revision >> 6) & 0x3f) + 32,
	       ((revision >> 0) & 0x3f) + 32);
	printf("Serial: %d\n", serial);
	printf("Tuner: %s\n", h_tuner[tvee[9]]);
	printf("Audio: %s\n", sndtype[tvee[b1 + 2 + b2 + 3]]);
//              printf("Radio:\n");
//              printf("Teletext: %s\n",teletext[tvee[38]]); /* ?????? */
	printf("Decoder: %s\n", chiptype[tvee[6]]);
	printf("EEPROM:\n");
	for (i = 0; i <= 40; i++) {
	    if (i == (b1 + 1) || i == (b1 + b2 + 2) 
		|| i == (b1 + b2 + b3 + 3))
		printf("\n");
	    printf("%02x ", tvee[i]);
	}
	printf("\n");
    }
}
    
/* ---------------------------------------------------------------------- */

void dump_buf(unsigned char *buf)
{
    int i,j;
    
    for (i = 0; i < 256; i += 16) {
	printf("%04x  ",i);
	for (j = i; j < i+16; j++) {
	    if (!(j%4))
		printf(" ");
	    printf("%02x ",buf[j]);
	}
	printf("  ");
	for (j = i; j < i+16; j++)
	    printf("%c",isalnum(buf[j]) ? buf[j] : '.');
	printf("\n");
    }
}

int parse_buf(unsigned char *buf)
{
    int  i,j,n,pos,count;
    char line[100];
    
    for (i = 0; i < 256; i += 16) {
	if (NULL == fgets(line,99,stdin)) {
	    fprintf(stderr,"unexpected EOF\n");
	    return -1;
	}
	if ('#' == line[0] || '\n' == line[0]) {
            i -= 16;
	    continue;
	}
	if (1 != sscanf(line,"%x%n",&n,&pos)) {
	    fprintf(stderr,"addr parse error (%d)\n",i>>4);
	    return -1;
	}
	if (n != i) {
	    fprintf(stderr,"addr mismatch\n");
	    return -1;
	}
	for (j = i; j < i+16; j++) {
	    if (1 != sscanf(line+pos,"%x%n",&n,&count)) {
		fprintf(stderr,"value parse error\n");
		return -1;
	    }
	    buf[j] = n;
	    pos += count;
	}
    }
    return 0;
}

/* ---------------------------------------------------------------------- */

void
read_buf(int fd, unsigned char *buf)
{
    int i,n=8; 
    unsigned char addr;
    
    for (i = 0; i < 256; i += n) {
	addr = i;
	if (-1 == (write(fd,&addr,1))) {
	    fprintf(stderr,"write addr %s: %s\n",dev,strerror(errno));
	    exit(1);
	}
	if (-1 == (read(fd,buf+i,n))) {
	    fprintf(stderr,"read data %s: %s\n",dev,strerror(errno));
	    exit(1);
	}
    }
}

void
write_buf(int fd, unsigned char *buf)
{
    int i,j,n = 8; 
    unsigned char tmp[17];
    
    for (i = 0; i < 256; i += n) {
	tmp[0] = i;
	for (j = 0; j < n; j++)
	    tmp[j+1] = buf[i+j];
	if (-1 == (write(fd,tmp,n+1))) {
	    fprintf(stderr," write data %s: %s\n",dev,strerror(errno));
	    exit(1);
	}
	fprintf(stderr,"*");
	usleep(100000); /* 0.1 sec */
    }
}

/* ---------------------------------------------------------------------- */

int main(int argc, char *argv[])
{
    int            f,c;
    unsigned char  buf[256];
    
    /* parse options */
    opterr=1;
    while ( (c=getopt(argc,argv,"hwxa:d:")) != -1) {
	switch (c){
	case 'w':
	    wr=1;
	    break;
	case 'x':
	    x=1;
	    break;
	case 'd':
	    dev = optarg;
	    break;
	case  'a':
	    adr = strtol(optarg,NULL,0);
	    break;
	case 'h':
	default:
	    usage(argv[0]);
	}
    }

    if (-1 == (f = open(dev,O_RDWR))) {
	fprintf(stderr,"open %s: %s\n",dev,strerror(errno));
	exit(1);
    }
    ioctl(f,I2C_SLAVE,adr>>1);
    memset(buf,0,256);

    if (isatty(fileno(stdin))) {
	/* read */
	read_buf(f,buf);
	dump_buf(buf);
	if (x)
	    dump_hauppauge(buf);
    } else {
	/* write */
	if (-1 == parse_buf(buf))
	    exit(1);
	dump_buf(buf);
	if (wr) {
	    fprintf(stderr,"writing to eeprom now... ");
	    write_buf(f,buf);
	    fprintf(stderr," ok\n");
	}
    }
    
    close(f);
    exit(0);
}
