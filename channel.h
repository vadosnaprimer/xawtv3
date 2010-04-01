#define CAPTURE_OFF          0
#define CAPTURE_OVERLAY      1
#define CAPTURE_GRABDISPLAY  2

struct CHANNEL {
    char  *name;
    char  *key;

    char  *cname;     /* name of the channel  */
    int   channel;    /* index into tvtuner[] */
    int   fine;
    int   freq;

    int   capture;
    int   source;
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

extern int    cur_sender, cur_channel, cur_fine, cur_norm, cur_input;

extern int            chan_tab;
extern struct STRTAB  chan_names[];

int  lookup_channel(char *channel);
int  get_freq(int i);
int  cf2freq(char *name, int fine);

void read_config();

/* ----------------------------------------------------------------------- */

extern struct STRTAB booltab[];
extern struct STRTAB normtab[];
extern struct STRTAB srctab[];

int str_to_int(char *str, struct STRTAB *tab);

