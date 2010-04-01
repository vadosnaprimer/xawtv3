#ifndef _SOUND_H_
#define _SOUND_H_

void* oss_open(char *device, struct ng_audio_fmt *fmt);
void oss_startrec(void *handle);
struct ng_audio_buf* oss_read(void *handle, long long stopby);
void oss_close(void *handle);

int  mixer_open(char *filename, char *device);
void mixer_close(void);
int  mixer_get_volume(void);
int  mixer_set_volume(int val);
int  mixer_mute(void);
int  mixer_unmute(void);
int  mixer_get_muted(void);

#endif /* _SOUND_H_ */
