void mjpg_rgb_init(int width, int height);
int  mjpg_rgb_compress(unsigned char *d, unsigned char *s, int p);
int  mjpg_bgr_compress(unsigned char *d, unsigned char *s, int p);
void mjpg_yuv_init(int width, int height);
int  mjpg_yuv422_compress(unsigned char *d, unsigned char *s, int p);
int  mjpg_yuv420_compress(unsigned char *d, unsigned char *s, int p);
void mjpg_cleanup(void);
