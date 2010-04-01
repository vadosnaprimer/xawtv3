#ifndef CAPTURE_H
#define CAPTURE_H

int   grab_syncer(int *talk, int quiet);
char* grab_initbuffers(int size, int count);
void  grab_freebuffers(char *buffers);

int   grab_writer_fork(int *talk);
int   grab_writer_avi(char *filename, struct MOVIE_PARAMS *params,
		      unsigned char *buffers, int bufsize, int quiet, int *talk);
void  grab_set_fps(int fps);
int   grab_putbuffer(int quiet, int writer, int wsync);

#endif /* CAPTURE_H */
