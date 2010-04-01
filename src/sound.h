#include "writeavi.h"

int   sound_open(struct MOVIE_PARAMS *params);
int   sound_bufsize();
void* sound_read();
void  sound_close();

int  mixer_open(char *filename, char *device);
void mixer_close();
int  mixer_get_volume();
int  mixer_set_volume(int val);
int  mixer_mute();
int  mixer_unmute();
int  mixer_get_muted();
