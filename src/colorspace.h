typedef int (*color_conv)(unsigned char *d, unsigned char *s, int p);

void lut_init(unsigned long red_mask, unsigned long green_mask,
	      unsigned long blue_mask, int depth, int swap);

int rgb24_to_lut2(unsigned char *dest, unsigned char *src, int p);
int bgr24_to_lut2(unsigned char *dest, unsigned char *src, int p);
int rgb32_to_lut2(unsigned char *dest, unsigned char *src, int p);
int bgr32_to_lut2(unsigned char *dest, unsigned char *src, int p);
int gray_to_lut2(unsigned char *dest, unsigned char *src, int p);

int rgb24_to_lut4(unsigned char *dest, unsigned char *src, int p);
int bgr24_to_lut4(unsigned char *dest, unsigned char *src, int p);
int rgb32_to_lut4(unsigned char *dest, unsigned char *src, int p);
int bgr32_to_lut4(unsigned char *dest, unsigned char *src, int p);
int gray_to_lut4(unsigned char *dest, unsigned char *src, int p);

/* ------------------------------------------------------------------- */
/* RGB conversions                                                     */

void swap_rgb24(char *mem, int n);
int rgb24_to_bgr24(unsigned char *dest, unsigned char *src, int p);
int bgr24_to_bgr32(unsigned char *dest, unsigned char *src, int p);
int bgr24_to_rgb32(unsigned char *dest, unsigned char *src, int p);
int rgb32_to_rgb24(unsigned char *dest, unsigned char *src, int p);
int rgb32_to_bgr24(unsigned char *dest, unsigned char *src, int p);
int byteswap_short(unsigned char *dest, unsigned char *src, int p);

/* ------------------------------------------------------------------- */
/* color => grayscale                                                  */

int rgb15_native_gray(unsigned char *dest, unsigned char *src, int p);
int rgb15_be_gray(unsigned char *dest, unsigned char *src, int p);
int rgb15_le_gray(unsigned char *dest, unsigned char *src, int p);

/* ------------------------------------------------------------------- */
/* YUV conversions                                                     */

int packed422_to_planar422(unsigned char *d, unsigned char *s, int p);
int packed422_to_planar420(unsigned char *d, unsigned char *s, int p);
#if 0
int packed422_to_planar411(unsigned char *d, unsigned char *s, int w, int h);
#endif
