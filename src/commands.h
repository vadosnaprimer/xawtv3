/* feedback for the user */
extern void (*update_title)(char *message);
extern void (*display_message)(char *message);

/* for updating GUI elements / whatever */
extern void (*norm_notify)(void);
extern void (*input_notify)(void);
extern void (*attr_notify)(int id);
extern void (*freqtab_notify)(void);
extern void (*setfreqtab_notify)(void);
extern void (*setstation_notify)(void);

/* gets called _before_ channel switches */
extern void (*channel_switch_hook)();

/* capture overlay/grab/off */
extern void (*set_capture_hook)(int old, int new);

/* toggle fullscreen */
extern void (*fullscreen_hook)();
extern void (*exit_hook)();
extern void (*reconfigure_hook)();

extern int do_overlay;
extern char *snapbase;
extern int x11_pixmap_format;
extern int grab_width, grab_height;

/*------------------------------------------------------------------------*/

#define MISSING_CAPTURE  1
#define MISSING_JPEG     2

void missing_feature(int id);

void attr_init();
void audio_init();
void audio_on();
void audio_off();
void set_defaults();

int do_va_cmd(int argc, ...);
int do_command(int argc, char **argv);
char** split_cmdline(char *line, int *count);
void keypad_timeout(void);
