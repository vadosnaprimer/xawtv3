extern int  x11_native_format;
extern int  x11_pixmap_format;
extern int  swidth,sheight;

typedef int   (*set_overlay)(int x, int y, int w, int h, int f,
			     struct OVERLAY_CLIP *oc, int count);
typedef void* (*get_frame)(void *dest, int width, int height);

void video_new_size();
void video_overlay(set_overlay);
int  video_displayframe(get_frame cb);
void video_setmax(int x, int y);

Widget video_init(Widget parent);
void video_close(void);
