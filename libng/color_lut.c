/*
 * colorspace conversion functions
 *    -- translate RGB using lookup tables
 *
 *  (c) 1998-2001 Gerd Knorr <kraxel@bytesex.org>
 *
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/time.h>
#ifdef HAVE_ENDIAN_H
# include <endian.h>
#endif

#include "grab-ng.h"
#include "byteswap.h"

static unsigned long   lut_red[256];
static unsigned long   lut_green[256];
static unsigned long   lut_blue[256];

/* ------------------------------------------------------------------- */

static int
rgb24_to_lut2(unsigned char *dest, unsigned char *src, int p)
{
    unsigned short *d = (unsigned short*)dest;

    while (p-- > 0) {
	*(d++) = lut_red[src[0]] | lut_green[src[1]] | lut_blue[src[2]];
	src += 3;
    }
    return ((unsigned char*)d-dest);
}

static int
bgr24_to_lut2(unsigned char *dest, unsigned char *src, int p)
{
    unsigned short *d = (unsigned short*)dest;

    while (p-- > 0) {
	*(d++) = lut_red[src[2]] | lut_green[src[1]] | lut_blue[src[0]];
	src += 3;
    }
    return ((unsigned char*)d-dest);
}

static int
rgb32_to_lut2(unsigned char *dest, unsigned char *src, int p)
{
    unsigned short *d = (unsigned short*)dest;

    while (p-- > 0) {
	*(d++) = lut_red[src[1]] | lut_green[src[2]] | lut_blue[src[3]];
	src += 4;
    }
    return ((unsigned char*)d-dest);
}

static int
bgr32_to_lut2(unsigned char *dest, unsigned char *src, int p)
{
    unsigned short *d = (unsigned short*)dest;

    while (p-- > 0) {
	*(d++) = lut_red[src[3]] | lut_green[src[2]] | lut_blue[src[1]];
	src += 4;
    }
    return ((unsigned char*)d-dest);
}

static int
gray_to_lut2(unsigned char *dest, unsigned char *src, int p)
{
    unsigned short *d = (unsigned short*)dest;

    while (p-- > 0) {
	*(d++) = lut_red[*src] | lut_green[*src] | lut_blue[*src];
	src++;
    }
    return ((unsigned char*)d-dest);
}

/* ------------------------------------------------------------------- */

static int
rgb24_to_lut4(unsigned char *dest, unsigned char *src, int p)
{
    unsigned int *d = (unsigned int*)dest;

    while (p-- > 0) {
	*(d++) = lut_red[src[0]] | lut_green[src[1]] | lut_blue[src[2]];
	src += 3;
    }
    return ((unsigned char*)d-dest);
}

static int
bgr24_to_lut4(unsigned char *dest, unsigned char *src, int p)
{
    unsigned int *d = (unsigned int*)dest;

    while (p-- > 0) {
	*(d++) = lut_red[src[2]] | lut_green[src[1]] | lut_blue[src[0]];
	src += 3;
    }
    return ((unsigned char*)d-dest);
}

static int
rgb32_to_lut4(unsigned char *dest, unsigned char *src, int p)
{
    unsigned int *d = (unsigned int*)dest;

    while (p-- > 0) {
	*(d++) = lut_red[src[1]] | lut_green[src[2]] | lut_blue[src[3]];
	src += 4;
    }
    return ((unsigned char*)d-dest);
}

static int
bgr32_to_lut4(unsigned char *dest, unsigned char *src, int p)
{
    unsigned int *d = (unsigned int*)dest;

    while (p-- > 0) {
	*(d++) = lut_red[src[3]] | lut_green[src[2]] | lut_blue[src[1]];
	src += 4;
    }
    return ((unsigned char*)d-dest);
}

static int
gray_to_lut4(unsigned char *dest, unsigned char *src, int p)
{
    unsigned int *d = (unsigned int*)dest;

    while (p-- > 0) {
	*(d++) = lut_red[*src] | lut_green[*src] | lut_blue[*src];
	src++;
    }
    return ((unsigned char*)d-dest);
}

/* ------------------------------------------------------------------- */

static struct ng_video_conv lut2_list[] = {
    {
	NG_GENERIC_PACKED,
	fmtid_in:	VIDEO_RGB24,
	priv:		rgb24_to_lut2,
    }, {
	NG_GENERIC_PACKED,
	fmtid_in:	VIDEO_BGR24,
	priv:		bgr24_to_lut2,
    }, {
	NG_GENERIC_PACKED,
	fmtid_in:	VIDEO_RGB32,
	priv:		rgb32_to_lut2,
    }, {
	NG_GENERIC_PACKED,
	fmtid_in:	VIDEO_BGR32,
	priv:		bgr32_to_lut2,
    }, {
	NG_GENERIC_PACKED,
	fmtid_in:	VIDEO_GRAY,
	priv:		gray_to_lut2,
    }
};

static struct ng_video_conv lut4_list[] = {
    {
	NG_GENERIC_PACKED,
	fmtid_in:	VIDEO_RGB24,
	priv:		rgb24_to_lut4,
    }, {
	NG_GENERIC_PACKED,
	fmtid_in:	VIDEO_BGR24,
	priv:		bgr24_to_lut4,
    }, {
	NG_GENERIC_PACKED,
	fmtid_in:	VIDEO_RGB32,
	priv:		rgb32_to_lut4,
    }, {
	NG_GENERIC_PACKED,
	fmtid_in:	VIDEO_BGR32,
	priv:		bgr32_to_lut4,
    }, {
	NG_GENERIC_PACKED,
	fmtid_in:	VIDEO_GRAY,
	priv:		gray_to_lut4,
    }
};

static const int nconv2 = sizeof(lut2_list)/sizeof(struct ng_video_conv);
static const int nconv4 = sizeof(lut4_list)/sizeof(struct ng_video_conv);

void
ng_lut_init(unsigned long red_mask, unsigned long green_mask,
	    unsigned long blue_mask, int fmtid, int swap)
{
    int             rgb_red_bits = 0;
    int             rgb_red_shift = 0;
    int             rgb_green_bits = 0;
    int             rgb_green_shift = 0;
    int             rgb_blue_bits = 0;
    int             rgb_blue_shift = 0;
    unsigned int    i;
    unsigned int    mask;

    for (i = 0; i < 32; i++) {
        mask = (1 << i);
        if (red_mask & mask)
            rgb_red_bits++;
        else if (!rgb_red_bits)
            rgb_red_shift++;
        if (green_mask & mask)
            rgb_green_bits++;
        else if (!rgb_green_bits)
            rgb_green_shift++;
        if (blue_mask & mask)
            rgb_blue_bits++;
        else if (!rgb_blue_bits)
            rgb_blue_shift++;
    }
#if 0
    printf("color: bits shift\n");
    printf("red  : %04i %05i\n", rgb_red_bits, rgb_red_shift);
    printf("green: %04i %05i\n", rgb_green_bits, rgb_green_shift);
    printf("blue : %04i %05i\n", rgb_blue_bits, rgb_blue_shift);
#endif
    
    if (rgb_red_bits > 8)
	for (i = 0; i < 256; i++)
	    lut_red[i] = (i << (rgb_red_bits + rgb_red_shift - 8));
    else
	for (i = 0; i < 256; i++)
	    lut_red[i] = (i >> (8 - rgb_red_bits)) << rgb_red_shift;
    
    if (rgb_green_bits > 8)
	for (i = 0; i < 256; i++)
	    lut_green[i] = (i << (rgb_green_bits + rgb_green_shift - 8));
    else
	for (i = 0; i < 256; i++)
	    lut_green[i] = (i >> (8 - rgb_green_bits)) << rgb_green_shift;
    
    if (rgb_blue_bits > 8)
	for (i = 0; i < 256; i++)
	    lut_blue[i] = (i << (rgb_blue_bits + rgb_blue_shift - 8));
    else
	for (i = 0; i < 256; i++)
	    lut_blue[i] = (i >> (8 - rgb_blue_bits)) << rgb_blue_shift;

    switch (ng_vfmt_to_depth[fmtid]) {
    case 2:
	if (swap) {
	    for (i = 0; i < 256; i++) {
		lut_red[i] = SWAP2(lut_red[i]);
		lut_green[i] = SWAP2(lut_green[i]);
		lut_blue[i] = SWAP2(lut_blue[i]);
	    }
	}
	for (i = 0; i < nconv2; i++)
	    lut2_list[i].fmtid_out = fmtid;
	ng_conv_register(lut2_list,nconv2);
	break;
    case 4:
	if (swap) {
	    for (i = 0; i < 256; i++) {
		lut_red[i] = SWAP4(lut_red[i]);
		lut_green[i] = SWAP4(lut_green[i]);
		lut_blue[i] = SWAP4(lut_blue[i]);
	    }
	}
	for (i = 0; i < nconv4; i++)
	    lut4_list[i].fmtid_out = fmtid;
	ng_conv_register(lut4_list,nconv4);
	break;
    }
}
