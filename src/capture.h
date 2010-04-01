#ifndef CAPTURE_H
#define CAPTURE_H

#define FIFO_MAX 64

struct FIFO {
    char *name;
    unsigned char *data[FIFO_MAX];
    int slots,read,write,eof;
    pthread_mutex_t lock;
    pthread_cond_t hasdata;
};

void fifo_init(struct FIFO *fifo, char *name, int slots);
int fifo_put(struct FIFO *fifo, void *data);
void* fifo_get(struct FIFO *fifo);

struct movie_handle*
movie_writer_init(char *moviename, char *audioname,
		  const struct ng_writer *writer, 
		  struct ng_video_fmt *video, const void *priv_video, int fps,
		  struct ng_audio_fmt *audio, const void *priv_audio, int slots);
int movie_writer_start(struct movie_handle*);
int movie_writer_stop(struct movie_handle*);

int movie_grab_put_video(struct movie_handle*);
int movie_grab_put_audio(struct movie_handle*);

#endif /* CAPTURE_H */
