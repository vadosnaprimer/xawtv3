#define TRAP(txt) fprintf(stderr,"%s:%d:%s\n",__FILE__,__LINE__,txt);exit(1);

/* ------------------------------------------------------------------------- */

extern int debug;
extern int have_dga;
extern int fd_grab;

/* ------------------------------------------------------------------------- */

void  grabber_run_v4l_conf(void);

int ng_grabber_setparams(struct ng_video_fmt *fmt, int fix_ratio);
struct ng_video_buf* ng_grabber_getimage(int single);
struct ng_video_buf* ng_grabber_convert(struct ng_video_buf *dest,
					struct ng_video_buf *buf);
struct ng_video_buf* ng_grabber_capture(struct ng_video_buf *dest,
					int single);
