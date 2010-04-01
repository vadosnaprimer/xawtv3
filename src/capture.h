#ifndef CAPTURE_H
#define CAPTURE_H

#define FIFO_MAX 64

struct FIFO {
    char *name;
    unsigned char *data[FIFO_MAX];
    unsigned int  size[FIFO_MAX];
    int slots,read,write,eof;
    pthread_mutex_t lock;
    pthread_cond_t hasdata;
};

void fifo_init(struct FIFO *fifo, char *name, int slots);
int fifo_put(struct FIFO *fifo, unsigned char *data, int size);
int fifo_get(struct FIFO *fifo, unsigned char **data);

int movie_writer_init(char *moviename, char *audioname,
		      const struct ng_writer *writer, 
		      struct ng_video_fmt *video, const void *priv_video, int fps,
		      struct ng_audio_fmt *audio, const void *priv_audio,
		      int slots, int *sound);
int movie_writer_start(void);
int movie_writer_stop(void);

int grab_put_video(void);
int grab_put_audio(void);

#endif /* CAPTURE_H */
