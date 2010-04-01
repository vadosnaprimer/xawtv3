#define CAPTURE_OFF          0
#define CAPTURE_OVERLAY      1
#define CAPTURE_GRABDISPLAY  2
#define CAPTURE_ON           9

struct CHANNEL {
    char  *name;
    char  *key;

    char  *cname;     /* name of the channel  */
    int   channel;    /* index into tvtuner[] */
    int   fine;
    int   freq;
    int   audio;

    int   capture;
    int   input;
    int   norm;

    int   color;
    int   bright;
    int   hue;
    int   contrast;

    Pixmap  pixmap;
    Widget  button;

    int ckey;
};

extern struct CHANNEL  defaults;
extern struct CHANNEL  **channels;
extern int             count;
extern int             have_mixer;

extern int have_config;
extern int jpeg_quality;
extern int keypad_ntsc;
extern int use_osd;
extern int fs_width,fs_height,fs_xoff,fs_yoff;
extern int pix_width,pix_height,pix_cols;
extern int last_sender, cur_sender;
extern int cur_channel, cur_fine, cur_norm, cur_input;
extern int cur_capture, cur_movie, cur_mute, cur_volume, cur_freq;

int  lookup_channel(char *channel);
int  get_freq(int i);
int  cf2freq(char *name, int fine);

struct CHANNEL* add_channel(char *name);
void hotkey_channel(struct CHANNEL *channel);
void configure_channel(struct CHANNEL *channel);
void del_channel(int nr);
void calc_frequencies(void);

void read_config(void);
void save_config(void);

/* ----------------------------------------------------------------------- */

struct LAUNCH {
    char *name;
    char *key;
    char *cmdline;
};

extern struct LAUNCH *launch;
extern int nlaunch;

/* ----------------------------------------------------------------------- */

extern struct STRTAB booltab[];
extern struct STRTAB captab[];

int str_to_int(char *str, struct STRTAB *tab);
const char* int_to_str(int n, struct STRTAB *tab);
int attr_to_int(char *attr);
