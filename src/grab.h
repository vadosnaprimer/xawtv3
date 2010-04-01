#define TRAP(txt) fprintf(stderr,"%s:%d:%s\n",__FILE__,__LINE__,txt);exit(1);

/* ------------------------------------------------------------------------- */

extern int debug;
extern int have_dga;

extern int fd_grab;
extern int grab_ratio_x;
extern int grab_ratio_y;

/* ------------------------------------------------------------------------- */

void  grabber_run_v4l_conf(void);
int   grabber_sw_rate(struct timeval *start, int fps, int count);
void  grabber_fix_ratio(int *width, int *height, int *xoff, int *yoff);
