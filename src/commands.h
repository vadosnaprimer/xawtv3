/* feedback for the user */
extern void (*update_title)(char *message);
extern void (*display_message)(char *message);
extern void (*vtx_message)(int argc, char **argv);

/* for updating GUI elements / whatever */
extern void (*attr_notify)(struct ng_attribute *attr, int val);
extern void (*volume_notify)(void);
extern void (*freqtab_notify)(void);
extern void (*setfreqtab_notify)(void);
extern void (*setstation_notify)(void);

/* gets called _before_ channel switches */
extern void (*channel_switch_hook)(void);

/* capture overlay/grab/off */
extern void (*set_capture_hook)(int old, int new);

/* toggle fullscreen */
extern void (*fullscreen_hook)(void);
extern void (*exit_hook)(void);
extern void (*capture_get_hook)(void);
extern void (*capture_rel_hook)(void);
extern void (*movie_hook)(int argc, char **argv);

extern int debug;
extern int do_overlay;
extern char *snapbase;
extern struct ng_video_fmt x11_fmt;
extern int cur_attrs[ATTR_ID_COUNT];

extern const struct ng_driver     *drv;
extern void                       *h_drv;
extern struct ng_attribute        *a_drv;
extern int                         f_drv;

/*------------------------------------------------------------------------*/

void attr_init(void);
void audio_init(void);
void audio_on(void);
void audio_off(void);
void set_defaults(void);

int do_va_cmd(int argc, ...);
int do_command(int argc, char **argv);
char** split_cmdline(char *line, int *count);
void keypad_timeout(void);
