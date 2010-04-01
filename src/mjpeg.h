void mjpg_rgb_init(int width, int height);
int  mjpg_rgb_compress(unsigned char *d, unsigned char *s, int p);
int  mjpg_bgr_compress(unsigned char *d, unsigned char *s, int p);
void mjpg_420_init(int width, int height);
int  mjpg_422_420_compress(unsigned char *d, unsigned char *s, int p);
int  mjpg_420_420_compress(unsigned char *d, unsigned char *s, int p);
void mjpg_422_init(int width, int height);
int  mjpg_422_422_compress(unsigned char *d, unsigned char *s, int p);
void mjpg_cleanup(void);
