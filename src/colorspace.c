/*
 * misc colorspace conversion functions
 *
 * most of them have common arguments (wanna be able to use function
 * pointers).  return value is the size of the resulting image.
 *
 *	int foo(unsigned char* dest, unsigned char* src, int pixels);
 *
 *  (c) 1998,99 Gerd Knorr <kraxel@goldbach.in-berlin.de>
 *
 */

#include "colorspace.h"
#include "byteorder.h"

/* ------------------------------------------------------------------- */
/* lut stuff                                                           */

static unsigned long   lut_red[256];
static unsigned long   lut_green[256];
static unsigned long   lut_blue[256];

void
lut_init(unsigned long red_mask, unsigned long green_mask,
	 unsigned long blue_mask, int depth, int swap)
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

    if (2 == depth && swap) {
	for (i = 0; i < 256; i++) {
	    lut_red[i] = SWAP2(lut_red[i]);
	    lut_green[i] = SWAP2(lut_green[i]);
	    lut_blue[i] = SWAP2(lut_blue[i]);
	}
    }
    if (4 == depth && swap) {
	for (i = 0; i < 256; i++) {
	    lut_red[i] = SWAP4(lut_red[i]);
	    lut_green[i] = SWAP4(lut_green[i]);
	    lut_blue[i] = SWAP4(lut_blue[i]);
	}
    }
}

int
rgb24_to_lut2(unsigned char *dest, unsigned char *src, int p)
{
    unsigned short *d = (unsigned short*)dest;

    while (p-- > 0) {
	*(d++) = lut_red[src[0]] | lut_green[src[1]] | lut_blue[src[2]];
	src += 3;
    }
    return ((unsigned char*)d-dest);
}

int
bgr24_to_lut2(unsigned char *dest, unsigned char *src, int p)
{
    unsigned short *d = (unsigned short*)dest;

    while (p-- > 0) {
	*(d++) = lut_red[src[2]] | lut_green[src[1]] | lut_blue[src[0]];
	src += 3;
    }
    return ((unsigned char*)d-dest);
}

int
rgb32_to_lut2(unsigned char *dest, unsigned char *src, int p)
{
    unsigned short *d = (unsigned short*)dest;

    while (p-- > 0) {
	*(d++) = lut_red[src[1]] | lut_green[src[2]] | lut_blue[src[3]];
	src += 4;
    }
    return ((unsigned char*)d-dest);
}

int
bgr32_to_lut2(unsigned char *dest, unsigned char *src, int p)
{
    unsigned short *d = (unsigned short*)dest;

    while (p-- > 0) {
	*(d++) = lut_red[src[3]] | lut_green[src[2]] | lut_blue[src[1]];
	src += 4;
    }
    return ((unsigned char*)d-dest);
}

int
gray_to_lut2(unsigned char *dest, unsigned char *src, int p)
{
    unsigned short *d = (unsigned short*)dest;

    while (p-- > 0) {
	*(d++) = lut_red[*src] | lut_green[*src] | lut_blue[*src];
	src++;
    }
    return ((unsigned char*)d-dest);
}

int
rgb24_to_lut4(unsigned char *dest, unsigned char *src, int p)
{
    unsigned int *d = (unsigned int*)dest;

    while (p-- > 0) {
	*(d++) = lut_red[src[0]] | lut_green[src[1]] | lut_blue[src[2]];
	src += 3;
    }
    return ((unsigned char*)d-dest);
}

int
bgr24_to_lut4(unsigned char *dest, unsigned char *src, int p)
{
    unsigned int *d = (unsigned int*)dest;

    while (p-- > 0) {
	*(d++) = lut_red[src[2]] | lut_green[src[1]] | lut_blue[src[0]];
	src += 3;
    }
    return ((unsigned char*)d-dest);
}

int
rgb32_to_lut4(unsigned char *dest, unsigned char *src, int p)
{
    unsigned int *d = (unsigned int*)dest;

    while (p-- > 0) {
	*(d++) = lut_red[src[1]] | lut_green[src[2]] | lut_blue[src[3]];
	src += 4;
    }
    return ((unsigned char*)d-dest);
}

int
bgr32_to_lut4(unsigned char *dest, unsigned char *src, int p)
{
    unsigned int *d = (unsigned int*)dest;

    while (p-- > 0) {
	*(d++) = lut_red[src[3]] | lut_green[src[2]] | lut_blue[src[1]];
	src += 4;
    }
    return ((unsigned char*)d-dest);
}

int
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
/* RGB conversions                                                     */

void
swap_rgb24(char *mem, int n)
{
    char  c;
    char *p = mem;
    int   i = n;
    
    while (--i) {
	c = p[0]; p[0] = p[2]; p[2] = c;
	p += 3;
    }
}

int
rgb24_to_bgr24(unsigned char *dest, unsigned char *src, int p)
{
    register unsigned char *s = src;
    register unsigned char *d = dest;

    while (--p) {
	*(d++) = s[2];
	*(d++) = s[1];
	*(d++) = s[0];
	s += 3;
    }
    return d-dest;
}

int
bgr24_to_bgr32(unsigned char *dest, unsigned char *src, int p)
{
    register unsigned char *s = src;
    register unsigned char *d = dest;

    while (p--) {
        *(d++) = *(s++);
        *(d++) = *(s++);
        *(d++) = *(s++);
	*(d++) = 0;
    }
    return d-dest;
}

int
bgr24_to_rgb32(unsigned char *dest, unsigned char *src, int p)
{
    register unsigned char *s = src;
    register unsigned char *d = dest;

    while (p--) {
	*(d++) = 0;
        *(d++) = s[2];
        *(d++) = s[1];
        *(d++) = s[0];
	s +=3;
    }
    return d-dest;
}

int
rgb32_to_rgb24(unsigned char *dest, unsigned char *src, int p)
{
    register unsigned char *s = src;
    register unsigned char *d = dest;

    while (p--) {
	s++;
	*(d++) = *(s++);
	*(d++) = *(s++);
	*(d++) = *(s++);
    }
    return d-dest;
}

int
rgb32_to_bgr24(unsigned char *dest, unsigned char *src, int p)
{
    register unsigned char *s = src;
    register unsigned char *d = dest;

    while (p--) {
	s++;
	d[2] = *(s++);
	d[1] = *(s++);
	d[0] = *(s++);
	d += 3;
    }
    return d-dest;
}

/* 15+16 bpp LE <=> BE */
int
byteswap_short(unsigned char *dest, unsigned char *src, int p)
{
    register unsigned char *s = src;
    register unsigned char *d = dest;

    while (--p) {
	*(d++) = s[1];
	*(d++) = s[0];
	s += 2;
    }
    return d-dest;
}

/* ------------------------------------------------------------------- */
/* color => grayscale                                                  */

int
rgb15_native_gray(unsigned char *dest, unsigned char *s, int p)
{
    int              r,g,b;
    unsigned short  *src = (unsigned short*)s;

    while (p--) {
	r = (src[0] & 0x7c00) >> 10;
	g = (src[0] & 0x03e0) >>  5;
	b =  src[1] & 0x001f;

	*(dest++) = ((3*r + 6*g + b)/10) << 3;
	src++;
    }
    return (unsigned char*)src-s;
}

int
rgb15_be_gray(unsigned char *dest, unsigned char *src, int p)
{
    register unsigned char *d = dest;

    while (p--) {
	unsigned char r = (src[0] & 0x7c) >> 2;
	unsigned char g = (src[0] & 0x03) << 3 | (src[1] & 0xe0) >> 5;
	unsigned char b = src[1] & 0x1f;

	*(d++) = ((3*r + 6*g + b)/10) << 3;
	src += 2;
    }
    return d-dest;
}

int
rgb15_le_gray(unsigned char *dest, unsigned char *src, int p)
{
    register unsigned char *d = dest;

    while (p--) {
	unsigned char r = (src[1] & 0x7c) >> 2;
	unsigned char g = (src[1] & 0x03) << 3 | (src[0] & 0xe0) >> 5;
	unsigned char b = src[0] & 0x1f;

	*(dest++) = ((3*r + 6*g + b)/10) << 3;
	src += 2;
    }
    return d-dest;
}

/* ------------------------------------------------------------------- */
/* YUV conversions                                                     */

int
packed422_to_planar422(unsigned char *d, unsigned char *s, int p)
{
    int i;
    unsigned char *y,*u,*v;

    i = p/2;
    y = d;
    u = y + p;
    v = u + p / 2;
    
    while (--i) {
	*(y++) = *(s++);
	*(u++) = *(s++);
	*(y++) = *(s++);
        *(v++) = *(s++);
    }
    return p*2;
}

/* y only, no chroma */
int
packed422_to_planar420(unsigned char *d, unsigned char *s, int p)
{
    int i;
    unsigned char *y;

    i = p/2;
    y = d;
    
    while (--i) {
	*(y++) = *(s++);
	s++;
	*(y++) = *(s++);
	s++;
    }
    return p*3/2;
}

#if 0
void
x_packed422_to_planar420(unsigned char *d, unsigned char *s, int w, int h)
{
    int  a,b;
    unsigned char *y,*u,*v;

    y = d;
    u = y + w * h;
    v = u + w * h / 4;

    for (a = h; a > 0; a -= 2) {
	for (b = w; b > 0; b -= 2) {
	    *(y++) = *(s++);
	    *(u++) = *(s++);
	    *(y++) = *(s++);
	    *(v++) = *(s++);
	}
	for (b = w; b > 0; b -= 2) {
	    *(y++) = *(s++);
	    s++;
	    *(y++) = *(s++);
	    s++;
	}
    }
}
#endif
