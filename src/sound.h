#ifndef _SOUND_H_
#define _SOUND_H_

void* oss_open(char *device, struct ng_audio_fmt *fmt);
int oss_startrec(void *handle);
struct ng_audio_buf* oss_read(void *handle, long long stopby);
void oss_levels(struct ng_audio_buf *buf, int *left, int *right);
int oss_fd(void *handle);
void oss_close(void *handle);

struct ng_attribute* mixer_open(char *filename, char *device);

#endif /* _SOUND_H_ */
