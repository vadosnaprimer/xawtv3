extern int  x11_native_format;
extern int  swidth,sheight;

typedef int (*set_overlay)(int x, int y, int w, int h, int f,
			   struct OVERLAY_CLIP *oc, int count);

void video_new_size();
void video_overlay(set_overlay);
void video_setmax(int x, int y);

Widget video_init(Widget parent);
void video_close(void);

