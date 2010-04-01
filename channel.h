struct CHANNEL {
    char  *name;
    char  *key;

    int   capture;
    int   source;
    int   norm;
    int   channel;
    int   fine;
    int   freq;

    int   color;
    int   bright;
    int   hue;
    int   contrast;
};

extern struct CHANNEL  defaults;
extern struct CHANNEL  **channels;
extern int             count;
extern int             have_mixer;

extern int    cur_sender, cur_channel, cur_fine, cur_norm;

int freq2chan(int f);
int cf2freq(int chan, int fine);
int freq2fine(int f);

void read_config();

/* ----------------------------------------------------------------------- */

struct STRTAB {
    int  nr;
    char *str;
};

extern struct STRTAB booltab[];
extern struct STRTAB normtab[];
extern struct STRTAB srctab[];

int str_to_int(char *str, struct STRTAB *tab);
void VolumeAction(Widget widget, XEvent *event,
		  String *params, Cardinal *num_params);

