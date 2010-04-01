/* NOTE!!!
 * The original driver was heavily modified to match the i2c interface
 * It was truncated to use the WinTV boards, too.
 *
 * Copyright (c) 1998 Richard Guenther <richard.guenther@student.uni-tuebingen.de>
 *
 * $Id: vtx.c,v 1.11 1998/03/29 18:39:20 root Exp $
 *
 * $Log: vtx.c,v $
 * Revision 1.11  1998/03/29 18:39:20  root
 * fixed one last bug, it seems, i2c is messing with the
 * parity bit, so you need a patch for VideoteXt-0.6 to
 * ignore the parity bit.
 *
 * Revision 1.10  1998/03/29 12:19:50  richard
 * bug fixes, cleanup
 *
 * Revision 1.9  1998/03/29 11:11:29  richard
 * Bugfixes, first (sort of) working version. We get (sometimes)
 * corrupted bytes, though... seems like a timing problem??
 *
 * Revision 1.8  1998/03/28 16:18:47  richard
 * fixes
 *
 *
 */

/*
 * vtx.c:
 * This is a loadable character-device-driver for videotext-interfaces
 * (aka teletext). Please check the Makefile/README for a list of supported
 * interfaces.
 *
 * Copyright (c) 1994-97 Martin Buck  <martin-2.buck@student.uni-ulm.de>
 *
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307,
 * USA.
 */

#include <linux/module.h>

#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/bios32.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <stdarg.h>
#include "i2c.h"
#include "vtx.h"



#define VTX_VER_MAJ 1
#define VTX_VER_MIN 7
#define VTX_NAME "videotext"

struct i2c_bus *bus = NULL;

static int videotext_attach(struct i2c_device *device)
{
  printk(KERN_DEBUG "videotext_attach: bus %p\n", device->bus);
  strcpy(device->name, VTX_NAME);
  bus = device->bus;
  return 0;
}

static int videotext_detach(struct i2c_device *device)
{
  printk(KERN_DEBUG "videotext_detach\n");
  return 0;
}

static int videotext_command(struct i2c_device *device,
			     unsigned int cmd, void *arg)
{
  printk(KERN_DEBUG "videotext_command\n");
  return -EINVAL;
}

/* new I2C driver support */
static struct i2c_driver i2c_driver_videotext = {
  VTX_NAME,     /* name */
  111,             /* FIXME: I2C_DRIVERID_VIDEOTEXT in i2c.h */
  34, 35,    /* FIXME: addr range */
  videotext_attach,
  videotext_detach,
  videotext_command
};



#ifndef PCI_VENDOR_ID_BROOKTREE
#define PCI_VENDOR_ID_BROOKTREE 0x109e
#endif
#ifndef PCI_DEVICE_ID_BT848
#define PCI_DEVICE_ID_BT848 0x350
#endif
#define VTX_PCI_VENDOR PCI_VENDOR_ID_BROOKTREE
#define VTX_PCI_DEVICE PCI_DEVICE_ID_BT848
#define NUM_DAUS 4
#define NUM_BUFS 8
#define IF_NAME "Hauppauge Win/TV PCI"


/* These variables are settable when loading the driver (with modutils-1.1.67 & up) */
unsigned int io_base;
unsigned char *pci_memio_base;

int major = VTX_DEFAULT_MAJOR;
#ifdef VTX_QUIET
int quiet = 1;
#else
int quiet = 0;
#endif


static int vtx_use_count;
static int is_searching[NUM_DAUS], disp_mode = DISPOFF;
static const int disp_modes[8][3] = {
  { 0x46, 0x03, 0x03 },	/* DISPOFF */
  { 0x46, 0xcc, 0xcc },	/* DISPNORM */
  { 0x44, 0x0f, 0x0f },	/* DISPTRANS */
  { 0x46, 0xcc, 0x46 },	/* DISPINS */
  { 0x44, 0x03, 0x03 },	/* DISPOFF, interlaced */
  { 0x44, 0xcc, 0xcc },	/* DISPNORM, interlaced */
  { 0x44, 0x0f, 0x0f },	/* DISPTRANS, interlaced */
  { 0x44, 0xcc, 0x46 }	/* DISPINS, interlaced */
};



#define PAGE_WAIT 30				/* Time in jiffies between requesting page and */
						/* checking status bits */
#define PGBUF_EXPIRE 1500			/* Time in jiffies to wait before retransmitting */
						/* page regardless of infobits */
typedef struct {
  byte_t pgbuf[VTX_VIRTUALSIZE];		/* Page-buffer */
  byte_t laststat[10];				/* Last value of infobits for DAU */
  byte_t sregs[7];				/* Page-request registers */
  unsigned long expire;				/* Time when page will be expired */
  unsigned clrfound : 1;			/* VTXIOCCLRFOUND has been called */
  unsigned stopped : 1;				/* VTXIOCSTOPDAU has been called */
} vdau_t;
static vdau_t vdau[NUM_DAUS];			/* Data for virtual DAUs (the 5249 only has one */
						/* real DAU, so we have to simulate some more) */


#define CCTWR 34		/* I²C write/read-address of vtx-chip */
#define CCTRD 35
#define NOACK_REPEAT 10		/* Retry access this many times on failure */
#define CLEAR_DELAY 5		/* Time in jiffies required to clear a page */
#define I2C_TIMEOUT 300		/* open/close/SDA-check timeout in jiffies */
#define READY_TIMEOUT 3		/* Time in jiffies to wait for ready signal of I²C-bus interface */
#define INIT_DELAY 500		/* Time in usec to wait at initialization of CEA interface */
#define START_DELAY 10		/* Time in usec to wait before starting write-cycle (CEA) */

#define VTX_DEV_MINOR 0

/* General defines and debugging support */

#ifndef FALSE
#define FALSE 0
#define TRUE 1
#endif
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

#define RESCHED \
        do { \
          if (need_resched) \
            schedule(); \
        } while (0)

#ifdef DEBUG_VTX
int debug = 1;
#define RETURN(val, str) \
        do { \
          if (debug > 1 || (debug == 1 && (val))) { \
            printk("%svtx: " str "\n", (val) ? KERN_ERR : KERN_DEBUG); \
          } \
          return (val); \
        } while (0)
#define RETURNx1(val, str, arg1) \
        do { \
          if (debug > 1 || (debug == 1 && (val))) { \
            printk("%svtx: " str "\n", (val) ? KERN_ERR : KERN_DEBUG, (arg1)); \
          } \
          return (val); \
        } while (0)
#define RETURNx2(val, str, arg1, arg2) \
        do { \
          if (debug > 1 || (debug == 1 && (val))) { \
            printk("%svtx: " str "\n", (val) ? KERN_ERR : KERN_DEBUG, (arg1), (arg2)); \
          } \
          return (val); \
        } while (0)
#define NOTIFY(level, str) \
        do { \
          if (debug >= (level)) { \
            printk("%svtx: " str "\n", (level) > 1 ? KERN_DEBUG : KERN_NOTICE); \
          } \
        } while (0)
#define NOTIFYx1(level, str, arg1) \
        do { \
          if (debug >= (level)) { \
            printk("%svtx: " str "\n", (level) > 1 ? KERN_DEBUG : KERN_NOTICE, (arg1)); \
          } \
        } while (0)
#define NOTIFYx2(level, str, arg1, arg2) \
        do { \
          if (debug >= (level)) { \
            printk("%svtx: " str "\n", (level) > 1 ? KERN_DEBUG : KERN_NOTICE, (arg1), (arg2)); \
          } \
        } while (0)
#else
#define RETURN(val, str) return (val)
#define RETURNx1(val, str, arg1) return (val)
#define RETURNx2(val, str, arg1, arg2) return (val)
#define NOTIFY(level, str) 
#define NOTIFYx1(level, str, arg1) 
#define NOTIFYx2(level, str, arg1, arg2) 
#endif


/* Wait the given number of jiffies (10ms). This calls the scheduler, so the actual
 * delay may be longer.
 */
static void
jdelay(unsigned long delay) {
  sigset_t oldblocked = current->blocked;

  spin_lock_irq(&current->sigmask_lock);
  sigfillset(&current->blocked);
  recalc_sigpending(current);
  spin_unlock_irq(&current->sigmask_lock);
  current->state = TASK_INTERRUPTIBLE;
  current->timeout = jiffies + delay;
  schedule();

  spin_lock_irq(&current->sigmask_lock);
  current->blocked = oldblocked;
  recalc_sigpending(current);
  spin_unlock_irq(&current->sigmask_lock);
}


/* Send arbitrary number of bytes to I²C-bus. Start & stop handshaking is done by this routine.
 * adr should be address of I²C-device, varargs-list of values to send must be terminated by -1
 * Returns -1 if I²C-device didn't send acknowledge, 0 otherwise
 */
static int
i2c_senddata(int adr, ...) {
  int val, loop;
  va_list argp;

  for (loop = 0; loop <= NOACK_REPEAT; loop++) {
    i2c_start(bus);
    if (i2c_sendbyte(bus, adr, 0) != 0)
      goto loopend;

    va_start(argp, adr);
    while ((val = va_arg(argp, int)) != -1) {
      if (val < 0 || val > 255) {
        printk(KERN_ERR "vtx: internal error in i2c_senddata\n");
        break;
      }
      if (i2c_sendbyte(bus, val, 0) != 0)
        goto loopend;
    }
    va_end(argp);
    i2c_stop(bus);
    return 0;
loopend:
    i2c_stop(bus);
    NOTIFYx1(1, "i2c_senddata: retry (loop=%d)", loop);
  }
  va_end(argp);
  return -1;
}


/* Send count number of bytes from buffer buf to I²C-device adr. Start & stop handshaking is
 * done by this routine. If uaccess is TRUE, data is read from user-space with get_user.
 * Returns -1 if I²C-device didn't send acknowledge, 0 otherwise
 */
static int
i2c_sendbuf(int adr, int reg, int count, byte_t *buf, int uaccess) {
  int pos, loop;
  byte_t val;

  for (loop = 0; loop <= NOACK_REPEAT; loop++) {
    i2c_start(bus);
    if (i2c_sendbyte(bus, adr, 0) != 0
	|| i2c_sendbyte(bus, reg, 0) != 0)
      goto loopend;
    for (pos = 0; pos < count; pos++) {
      if (uaccess)
        get_user(val, buf + pos);
      else
        val = buf[pos];
      if (i2c_sendbyte(bus, val, 0) != 0)
        goto loopend;
      RESCHED;
    }
    i2c_stop(bus);
    return 0;
loopend:
    i2c_stop(bus);
    NOTIFYx1(1, "i2c_sendbuf: retry (loop=%d)", loop);
  }
  return -1;
}


/* Get count number of bytes from I²C-device at address adr, store them in buf. Start & stop
 * handshaking is done by this routine, ack will be sent after the last byte to inhibit further
 * sending of data. If uaccess is TRUE, data is written to user-space with put_user.
 * Returns -1 if I²C-device didn't send acknowledge, 0 otherwise
 */
static int
i2c_getdata(int adr, int count, byte_t *buf, int uaccess) {
  int pos, loop, val;

  for (loop = 0; loop <= NOACK_REPEAT; loop++) {
    i2c_start(bus);
    if (i2c_sendbyte(bus, adr, 1) != 0)
      goto loopend;
    for (pos = 0; pos < count; pos++) {
      val = i2c_readbyte(bus, (pos==count-1) ? 1 : 0);
      if (uaccess) {
        put_user(val, buf + pos);
      } else {
        buf[pos] = val;
      }
      RESCHED;
    }
    i2c_stop(bus);
    return 0;
loopend:
    i2c_stop(bus);
    NOTIFYx1(1, "i2c_getdata: retry (loop=%d)", loop);
  }
  return -1;
}


/* Standard character-device-driver functions
 */

static int
vtx_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg) {
  int err;
  static int virtual_mode = FALSE;

  NOTIFY(2, "vtx_ioctl");

  switch(cmd) {
    case VTXIOCGETINFO: {
      vtx_info_t info;
      
      info.version_major = VTX_VER_MAJ;
      info.version_minor = VTX_VER_MIN;
      info.numpages = NUM_DAUS;
      /*info.cct_type = CCT_TYPE;*/
      if ((err = verify_area(VERIFY_WRITE, (void*)arg, sizeof(vtx_info_t))))
        RETURN(err, "VTXIOCGETINFO: EFAULT");
      copy_to_user((void*)arg, &info, sizeof(vtx_info_t));
      RETURN(0, "VTXIOCGETINFO: OK");
    }

    case VTXIOCCLRPAGE: {
      vtx_pagereq_t req;
      
      if ((err = verify_area(VERIFY_READ, (void*)arg, sizeof(vtx_pagereq_t))))
        RETURN(err, "VTXIOCCLRPAGE: EFAULT");
      copy_from_user(&req, (void*)arg, sizeof(vtx_pagereq_t));
      if (req.pgbuf < 0 || req.pgbuf >= NUM_DAUS)
        RETURN(-EINVAL, "VTXIOCCLRPAGE: EINVAL");
      memset(vdau[req.pgbuf].pgbuf, ' ', sizeof(vdau[0].pgbuf));
      vdau[req.pgbuf].clrfound = TRUE;
      RETURN(0, "VTXIOCCLRPAGE: OK");
    }

    case VTXIOCCLRFOUND: {
      vtx_pagereq_t req;
      
      if ((err = verify_area(VERIFY_READ, (void*)arg, sizeof(vtx_pagereq_t))))
        RETURN(err, "VTXIOCCLRFOUND: EFAULT");
      copy_from_user(&req, (void*)arg, sizeof(vtx_pagereq_t));
      if (req.pgbuf < 0 || req.pgbuf >= NUM_DAUS)
        RETURN(-EINVAL, "VTXIOCCLRFOUND: EINVAL");
      vdau[req.pgbuf].clrfound = TRUE;
      RETURN(0, "VTXIOCCLRFOUND: OK");
    }

    case VTXIOCPAGEREQ: {
      vtx_pagereq_t req;
      
      if ((err = verify_area(VERIFY_READ, (void*)arg, sizeof(vtx_pagereq_t))))
        RETURN(err, "VTXIOCPAGEREQ: EFAULT");
      copy_from_user(&req, (void*)arg, sizeof(vtx_pagereq_t));
      if (!(req.pagemask & PGMASK_PAGE))
        req.page = 0;
      if (!(req.pagemask & PGMASK_HOUR))
        req.hour = 0;
      if (!(req.pagemask & PGMASK_MINUTE))
        req.minute = 0;
      if (req.page < 0 || req.page > 0x8ff)
        RETURN(-EINVAL, "VTXIOCPAGEREQ: EINVAL (1)");
      req.page &= 0x7ff;
      if (req.hour < 0 || req.hour > 0x3f || req.minute < 0 || req.minute > 0x7f ||
          req.pagemask < 0 || req.pagemask >= PGMASK_MAX || req.pgbuf < 0 || req.pgbuf >= NUM_DAUS)
        RETURN(-EINVAL, "VTXIOCPAGEREQ: EINVAL (2)");
      vdau[req.pgbuf].sregs[0] = (req.pagemask & PG_HUND ? 0x10 : 0) | (req.page / 0x100);
      vdau[req.pgbuf].sregs[1] = (req.pagemask & PG_TEN ? 0x10 : 0) | ((req.page / 0x10) & 0xf);
      vdau[req.pgbuf].sregs[2] = (req.pagemask & PG_UNIT ? 0x10 : 0) | (req.page & 0xf);
      vdau[req.pgbuf].sregs[3] = (req.pagemask & HR_TEN ? 0x10 : 0) | (req.hour / 0x10);
      vdau[req.pgbuf].sregs[4] = (req.pagemask & HR_UNIT ? 0x10 : 0) | (req.hour & 0xf);
      vdau[req.pgbuf].sregs[5] = (req.pagemask & MIN_TEN ? 0x10 : 0) | (req.minute / 0x10);
      vdau[req.pgbuf].sregs[6] = (req.pagemask & MIN_UNIT ? 0x10 : 0) | (req.minute & 0xf);
      vdau[req.pgbuf].stopped = FALSE;
      vdau[req.pgbuf].clrfound = TRUE;
      is_searching[req.pgbuf] = TRUE;
      RETURN(0, "VTXIOCPAGEREQ: OK");
    }

    case VTXIOCGETSTAT: {
      vtx_pagereq_t req;
      byte_t infobits[10];
      vtx_pageinfo_t info;
      int a;

      if ((err = verify_area(VERIFY_READ, (void*)arg, sizeof(vtx_pagereq_t))))
        RETURN(err, "VTXIOCGETSTAT: EFAULT (read)");
      copy_from_user(&req, (void*)arg, sizeof(vtx_pagereq_t));
      if (req.pgbuf < 0 || req.pgbuf >= NUM_DAUS)
        RETURN(-EINVAL, "VTXIOCGETSTAT: EINVAL");
      if (!vdau[req.pgbuf].stopped) {
        if (i2c_senddata(CCTWR, 2, 0, -1) ||
            i2c_sendbuf(CCTWR, 3, sizeof(vdau[0].sregs), vdau[req.pgbuf].sregs, FALSE) ||
            i2c_senddata(CCTWR, 8, 0, 25, 0, ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', -1) ||
            i2c_senddata(CCTWR, 2, 0, vdau[req.pgbuf].sregs[0] | 8, -1) ||
            i2c_senddata(CCTWR, 8, 0, 25, 0, -1))
          RETURN(-EIO, "VTXIOCGETSTAT: EIO (pagereq)");
        jdelay(PAGE_WAIT);
        if (i2c_getdata(CCTRD, 10, infobits, FALSE))
          RETURN(-EIO, "VTXIOCGETSTAT: EIO (getinfo)");

        if (!(infobits[8] & 0x10) && !(infobits[7] & 0xf0) &&	/* check FOUND-bit */
            (memcmp(infobits, vdau[req.pgbuf].laststat, sizeof(infobits)) || 
            jiffies >= vdau[req.pgbuf].expire)) {		/* check if new page arrived */
          if (i2c_senddata(CCTWR, 8, 0, 0, 0, -1) ||
              i2c_getdata(CCTRD, VTX_PAGESIZE, vdau[req.pgbuf].pgbuf, FALSE))
            RETURN(-EIO, "VTXIOCGETSTAT: EIO (normal_page)");
          vdau[req.pgbuf].expire = jiffies + PGBUF_EXPIRE;
          memset(vdau[req.pgbuf].pgbuf + VTX_PAGESIZE, ' ', VTX_VIRTUALSIZE - VTX_PAGESIZE);
          if (virtual_mode) {
            /* Packet X/24 */
            if (i2c_senddata(CCTWR, 8, 0, 0x20, 0, -1) ||
                i2c_getdata(CCTRD, 40, vdau[req.pgbuf].pgbuf + VTX_PAGESIZE + 20 * 40, FALSE))
              RETURN(-EIO, "VTXIOCGETSTAT: EIO (virtual_row_24)");
            /* Packet X/27/0 */
            if (i2c_senddata(CCTWR, 8, 0, 0x21, 0, -1) ||
                i2c_getdata(CCTRD, 40, vdau[req.pgbuf].pgbuf + VTX_PAGESIZE + 16 * 40, FALSE))
              RETURN(-EIO, "VTXIOCGETSTAT: EIO (virtual_row_27)");
            /* Packet 8/30/0...8/30/15
             * FIXME: AFAIK, the 5249 does hamming-decoding for some bytes in packet 8/30,
             *        so we should undo this here.
             */
            if (i2c_senddata(CCTWR, 8, 0, 0x22, 0, -1) ||
                i2c_getdata(CCTRD, 40, vdau[req.pgbuf].pgbuf + VTX_PAGESIZE + 23 * 40, FALSE))
              RETURN(-EIO, "VTXIOCGETSTAT: EIO (virtual_row_30)");
          }
          vdau[req.pgbuf].clrfound = FALSE;
          memcpy(vdau[req.pgbuf].laststat, infobits, sizeof(infobits));
        } else {
          memcpy(infobits, vdau[req.pgbuf].laststat, sizeof(infobits));
        }
      } else {
        memcpy(infobits, vdau[req.pgbuf].laststat, sizeof(infobits));
      }

      info.pagenum = ((infobits[8] << 8) & 0x700) | ((infobits[1] << 4) & 0xf0) |
          (infobits[0] & 0x0f);
      if (info.pagenum < 0x100)
        info.pagenum += 0x800;
      info.hour = ((infobits[5] << 4) & 0x30) | (infobits[4] & 0x0f);
      info.minute = ((infobits[3] << 4) & 0x70) | (infobits[2] & 0x0f);
      info.charset = ((infobits[7] >> 1) & 7);
      info.delete = !!(infobits[3] & 8);
      info.headline = !!(infobits[5] & 4);
      info.subtitle = !!(infobits[5] & 8);
      info.supp_header = !!(infobits[6] & 1);
      info.update = !!(infobits[6] & 2);
      info.inter_seq = !!(infobits[6] & 4);
      info.dis_disp = !!(infobits[6] & 8);
      info.serial = !!(infobits[7] & 1);
      info.notfound = !!(infobits[8] & 0x10);
      info.pblf = !!(infobits[9] & 0x20);
      info.hamming = 0;
      for (a = 0; a <= 7; a++) {
        if (infobits[a] & 0xf0) {
          info.hamming = 1;
          break;
        }
      }
      if (vdau[req.pgbuf].clrfound)
        info.notfound = 1;
      if ((err = verify_area(VERIFY_WRITE, req.buffer, sizeof(vtx_pageinfo_t))))
        RETURN(err, "VTXIOCGETSTAT: EFAULT (write)");
      copy_to_user(req.buffer, &info, sizeof(vtx_pageinfo_t));
      if (!info.hamming && !info.notfound) {
        is_searching[req.pgbuf] = FALSE;
      }
      RETURN(0, "VTXIOCGETSTAT: OK");
    }

    case VTXIOCGETPAGE: {
      vtx_pagereq_t req;
      int start, end;

      if ((err = verify_area(VERIFY_READ, (void*)arg, sizeof(vtx_pagereq_t))))
        RETURN(err, "VTXIOCGETPAGE: EFAULT (read)");
      copy_from_user(&req, (void*)arg, sizeof(vtx_pagereq_t));
      if (req.pgbuf < 0 || req.pgbuf >= NUM_DAUS || req.start < 0 ||
          req.start > req.end || req.end >= (virtual_mode ? VTX_VIRTUALSIZE : VTX_PAGESIZE))
        RETURN(-EINVAL, "VTXIOCGETPAGE: EINVAL");
      if ((err = verify_area(VERIFY_WRITE, req.buffer, req.end - req.start + 1)))
        RETURN(err, "VTXIOCGETPAGE: EFAULT (write)");
      copy_to_user(req.buffer, &vdau[req.pgbuf].pgbuf[req.start], req.end - req.start + 1);
      /* Always read the time directly from SAA5249
       */
      if (req.start <= 39 && req.end >= 32) {
        start = MAX(req.start, 32);
        end = MIN(req.end, 39);
        if (i2c_senddata(CCTWR, 8, 0, 0, start, -1) ||
            i2c_getdata(CCTRD, end - start + 1, req.buffer + start - req.start, TRUE))
          RETURN(-EIO, "VTXIOCGETPAGE: EIO (time)");
      }
      /* Insert the current header if DAU is still searching for a page */
      if (req.start <= 31 && req.end >= 7 && is_searching[req.pgbuf]) {
        start = MAX(req.start, 7);
        end = MIN(req.end, 31);
        if (i2c_senddata(CCTWR, 8, 0, 0, start, -1) ||
            i2c_getdata(CCTRD, end - start + 1, req.buffer + start - req.start, TRUE))
          RETURN(-EIO, "VTXIOCGETPAGE: EIO (header)");
      }
      RETURN(0, "VTXIOCGETPAGE: OK");
    }

    case VTXIOCSTOPDAU: {
      vtx_pagereq_t req;
      
      if ((err = verify_area(VERIFY_READ, (void*)arg, sizeof(vtx_pagereq_t))))
        RETURN(err, "VTXIOCSTOPDAU: EFAULT");
      copy_from_user(&req, (void*)arg, sizeof(vtx_pagereq_t));
      if (req.pgbuf < 0 || req.pgbuf >= NUM_DAUS)
        RETURN(-EINVAL, "VTXIOCSTOPDAU: EINVAL");
      vdau[req.pgbuf].stopped = TRUE;
      is_searching[req.pgbuf] = FALSE;
      RETURN(0, "VTXIOCSTOPDAU: OK");
    }

    case VTXIOCPUTPAGE: {
      RETURN(0, "VTXIOCPUTPAGE: OK (dummy)");
    }

    case VTXIOCSETDISP: {
      RETURN(0, "VTXIOCSETDISP: OK (dummy)");
    }

    case VTXIOCPUTSTAT: {
      RETURN(0, "VTXIOCPUTSTAT: OK (dummy)");
    }

    case VTXIOCCLRCACHE: {
      if (i2c_senddata(CCTWR, 0, NUM_DAUS, 0, 8, -1) || i2c_senddata(CCTWR, 11,
          ' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',
          ' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ', -1))
        RETURN(-EIO, "VTXIOCCLRCACHE (clear_header)");
      if (i2c_senddata(CCTWR, 3, 0x20, -1))
        RETURN(-EIO, "VTXIOCCLRCACHE: EIO (clear_cache)");
      jdelay(10 * CLEAR_DELAY);			/* I have no idea how long we have to wait here */
      RETURN(0, "VTXIOCCLRCACHE: OK");
    }

    case VTXIOCSETVIRT: {
      /* The SAA5249 has virtual-row reception turned on always */
      virtual_mode = arg;
      RETURN(0, "VTXIOCSETVIRTUAL: OK (dummy)");
    }
  }
  RETURN(-EINVAL, "vtx_ioctl: EINVAL (unknown command)");
}


static int
common_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg) {
  int retval;

  NOTIFY(2, "common_ioctl");
  if (MINOR(inode->i_rdev) == VTX_DEV_MINOR) {
    retval = vtx_ioctl(inode, file, cmd, arg);
    return retval;
  }
  RETURN(-ENODEV, "common_ioctl: ENODEV  *** can't happen ***");
}


static int
vtx_open(struct inode *inode, struct file *file) {
  int pgbuf;

  if (vtx_use_count++) {
    RETURN(-EBUSY, "vtx_open: EBUSY (vtx_in_use)");
  }

  if (!bus) {
    RETURN(-ENODEV, "vtx_open: ENODEV (could not attach to i2c bus)");
  }

  if (i2c_senddata(CCTWR, 0, 0, -1) ||		/* Select R11 */
						/* Turn off parity checks (we do this ourselves) */
      i2c_senddata(CCTWR, 1, disp_modes[disp_mode][0], 0, -1) ||
						/* Display TV-picture, no virtual rows */
      i2c_senddata(CCTWR, 4, NUM_DAUS, disp_modes[disp_mode][1], disp_modes[disp_mode][2], 7, -1)) {
						/* Set display to page 4 */
    RETURN(-EIO, "vtx_open: EIO (init)");
  }

  for (pgbuf = 0; pgbuf < NUM_DAUS; pgbuf++) {
    memset(vdau[pgbuf].pgbuf, ' ', sizeof(vdau[0].pgbuf));
    memset(vdau[pgbuf].sregs, 0, sizeof(vdau[0].sregs));
    memset(vdau[pgbuf].laststat, 0, sizeof(vdau[0].laststat));
    vdau[pgbuf].expire = 0;
    vdau[pgbuf].clrfound = TRUE;
    vdau[pgbuf].stopped = TRUE;
    is_searching[pgbuf] = FALSE;
  }

  RETURN(0, "vtx_open: OK");
}



static int
common_open(struct inode *inode, struct file *file) {
  int retval;

  NOTIFY(2, "common_open");
  MOD_INC_USE_COUNT;

  if (MINOR(inode->i_rdev) == VTX_DEV_MINOR) {
    if ((retval = vtx_open(inode, file)) < 0) {
      vtx_use_count--;
      MOD_DEC_USE_COUNT;
    }
    return retval;
  }
  MOD_DEC_USE_COUNT;
  RETURN(-ENODEV, "common_open: ENODEV");
}


static int
common_release(struct inode *inode, struct file *file) {
  NOTIFY(2, "common_release");

  if (MINOR(inode->i_rdev) == VTX_DEV_MINOR) {
    i2c_senddata(CCTWR, 1, 0x20, -1);		/* Turn off CCT */
    i2c_senddata(CCTWR, 5, 3, 3, -1);		/* Turn off TV-display */
  }
  if (MINOR(inode->i_rdev) == VTX_DEV_MINOR) {
    vtx_use_count--;
  }
  MOD_DEC_USE_COUNT;
  return 0;
}


static struct file_operations vtx_fops = {
  NULL,		/* lseek */
  NULL,		/* read */
  NULL,		/* write */
  NULL,		/* readdir */
  NULL,		/* poll */
  common_ioctl,
  NULL,		/* mmap */
  common_open,
  common_release,
};


/* Routines for loadable modules
 */

int
init_module(void) {
  unsigned char pci_bus, pci_dev_fn;

  if (!quiet) {
    printk(KERN_INFO "videotext driver (" IF_NAME " interface)  version %d.%d\n",
        VTX_VER_MAJ, VTX_VER_MIN);
  }
  if (pcibios_find_device(VTX_PCI_VENDOR, VTX_PCI_DEVICE, 0, &pci_bus, &pci_dev_fn)) {
    printk(KERN_ERR "vtx: No PCI videotext device found\n");
    return -EIO;
  }
  pcibios_read_config_dword(pci_bus, pci_dev_fn, PCI_BASE_ADDRESS_0, &io_base);
  pci_memio_base = ioremap(io_base & PCI_BASE_ADDRESS_MEM_MASK, 4096);
  if (!quiet) {
    printk(KERN_INFO "vtx: PCI device at bus %u, device %u, function %u, address 0x%x\n",
        pci_bus, PCI_SLOT(pci_dev_fn), PCI_FUNC(pci_dev_fn), io_base);
  }
  if (register_chrdev(major, VTX_NAME, &vtx_fops)) {
    printk(KERN_ERR "vtx: Error: Cannot register major number %d\n", major);
    iounmap(pci_memio_base);
    return -EIO;
  }

  i2c_register_driver(&i2c_driver_videotext);

  return 0;
}


void
cleanup_module(void) {
  if (!quiet) {
    printk(KERN_INFO "removing videotext driver\n");
  }
  iounmap(pci_memio_base);

  i2c_unregister_driver(&i2c_driver_videotext);

  if (unregister_chrdev(major, VTX_NAME))
    printk(KERN_WARNING "vtx: Warning: unregister_chrdev() failed\n");
}
