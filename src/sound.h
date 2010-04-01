#ifndef _SOUND_H_
#define _SOUND_H_

int   sound_open(struct ng_audio_fmt *fmt);
int   sound_bufsize(void);
void  sound_startrec(void);
void  sound_read(char *buffer);
void  sound_close(void);

int  mixer_open(char *filename, char *device);
void mixer_close(void);
int  mixer_get_volume(void);
int  mixer_set_volume(int val);
int  mixer_mute(void);
int  mixer_unmute(void);
int  mixer_get_muted(void);

#endif /* _SOUND_H_ */
