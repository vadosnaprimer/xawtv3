extern char *webcam;
int webcam_put(char *filename, int format, int width, int height,
	       char *data, int size);
void webcam_init(void);
void webcam_exit(void);
