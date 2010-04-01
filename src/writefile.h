int   patch_up(char *name);
char* snap_filename(char *base, char *channel, char *ext);
int   write_jpeg(char *filename, char *data, int width, int height, int quality, int gray);
int   write_ppm(char *filename, char *data, int width, int height);
int   write_pgm(char *filename, char *data, int width, int height);
