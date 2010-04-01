int  mixer_open(char *filename, char *device);
void mixer_close();
int  mixer_get_volume();
int  mixer_set_volume(int val);
int  mixer_mute();
int  mixer_unmute();
int  mixer_get_muted();
