/*
 * save pictures to disk (ppm,pgm,jpeg)
 *
 *  (c) 1998-2000 Gerd Knorr <kraxel@bytesex.org>
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include <sys/param.h>
#include <jpeglib.h>
#ifdef HAVE_ENDIAN_H
# include <endian.h>
#endif
#include "byteorder.h"

#include "grab-ng.h"
#include "writefile.h"

/* ---------------------------------------------------------------------- */

/*
 * count up the latest block of digits in the passed string
 * (used for filename numbering
 */
int
patch_up(char *name)
{
    char *ptr;
    
    for (ptr = name+strlen(name); ptr >= name; ptr--)
	if (isdigit(*ptr))
	    break;
    if (ptr < name)
	return 0;
    while (*ptr == '9' && ptr >= name)
	*(ptr--) = '0';
    if (ptr < name)
	return 0;
    if (isdigit(*ptr)) {
	(*ptr)++;
	return 1;
    }
    return 0;
}

char*
snap_filename(char *base, char *channel, char *ext)
{
    static time_t last = 0;
    static int count = 0;
    static char *filename = NULL;
    
    time_t now;
    struct tm* tm;
    char timestamp[32];
    
    time(&now);
    tm = localtime(&now);
    
    if (last != now)
	count = 0;
    last = now;
    count++;
    
    if (filename != NULL)
	free(filename);	
    filename = malloc(strlen(base)+strlen(channel)+strlen(ext)+32);
    
    strftime(timestamp,31,"%Y%m%d-%H%M%S",tm);
    sprintf(filename,"%s-%s-%s-%d.%s",
	    base,channel,timestamp,count,ext);
    return filename;
}

/* ---------------------------------------------------------------------- */

int write_jpeg(char *filename, char *data, int width, int height, int quality, int gray)
{
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    FILE *fp;
    int i;
    unsigned char *line;
    int line_length;

    if (NULL == (fp = fopen(filename,"w"))) {
	fprintf(stderr,"grab: can't open %s: %s\n",filename,strerror(errno));
	return -1;
    }

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);
    jpeg_stdio_dest(&cinfo, fp);
    cinfo.image_width = width;
    cinfo.image_height = height;
    cinfo.input_components = gray ? 1: 3;
    cinfo.in_color_space = gray ? JCS_GRAYSCALE: JCS_RGB;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality, TRUE);
    jpeg_start_compress(&cinfo, TRUE);

    line_length = gray ? width: width * 3;
    for (i = 0, line = data; i < height; i++, line += line_length)
	jpeg_write_scanlines(&cinfo, &line, 1);
    
    jpeg_finish_compress(&(cinfo));
    jpeg_destroy_compress(&(cinfo));
    fclose(fp);

    return 0;
}

int write_ppm(char *filename, char *data, int width, int height)
{
    FILE *fp;
    
    if (NULL == (fp = fopen(filename,"w"))) {
	fprintf(stderr,"grab: can't open %s: %s\n",filename,strerror(errno));
	return -1;
    }
    fprintf(fp,"P6\n%d %d\n255\n",width,height);
    fwrite(data,height,3*width,fp);
    fclose(fp);

    return 0;
}

int write_pgm(char *filename, char *data, int width, int height)
{
    FILE *fp;
    
    if (NULL == (fp = fopen(filename,"w"))) {
	fprintf(stderr,"grab: can't open %s: %s\n",filename,strerror(errno));
	return -1;
    }
    fprintf(fp,"P5\n%d %d\n255\n",width,height);
    fwrite(data,height,width,fp);
    fclose(fp);

    return 0;
}

/* ---------------------------------------------------------------------- */
/* *.wav I/O stolen from cdda2wav                                         */

/* Copyright (C) by Heiko Eissfeldt */

typedef unsigned char  BYTE;
typedef unsigned short WORD;
typedef unsigned long  DWORD;
typedef unsigned long  FOURCC;	/* a four character code */

/* flags for 'wFormatTag' field of WAVEFORMAT */
#define WAVE_FORMAT_PCM 1

/* MMIO macros */
#define mmioFOURCC(ch0, ch1, ch2, ch3) \
  ((DWORD)(BYTE)(ch0) | ((DWORD)(BYTE)(ch1) << 8) | \
  ((DWORD)(BYTE)(ch2) << 16) | ((DWORD)(BYTE)(ch3) << 24))

#define FOURCC_RIFF	mmioFOURCC ('R', 'I', 'F', 'F')
#define FOURCC_LIST	mmioFOURCC ('L', 'I', 'S', 'T')
#define FOURCC_WAVE	mmioFOURCC ('W', 'A', 'V', 'E')
#define FOURCC_FMT	mmioFOURCC ('f', 'm', 't', ' ')
#define FOURCC_DATA	mmioFOURCC ('d', 'a', 't', 'a')

typedef struct CHUNKHDR {
    FOURCC ckid;		/* chunk ID */
    DWORD dwSize; 	        /* chunk size */
} CHUNKHDR;

/* simplified Header for standard WAV files */
typedef struct WAVEHDR {
    CHUNKHDR chkRiff;
    FOURCC fccWave;
    CHUNKHDR chkFmt;
    WORD wFormatTag;	   /* format type */
    WORD nChannels;	   /* number of channels (i.e. mono, stereo, etc.) */
    DWORD nSamplesPerSec;  /* sample rate */
    DWORD nAvgBytesPerSec; /* for buffer estimation */
    WORD nBlockAlign;	   /* block size of data */
    WORD wBitsPerSample;
    CHUNKHDR chkData;
} WAVEHDR;

#if __BYTE_ORDER == __BIG_ENDIAN
# define cpu_to_le32(x) SWAP4((x))
# define cpu_to_le16(x) SWAP2((x))
# define le32_to_cpu(x) SWAP4((x))
# define le16_to_cpu(x) SWAP2((x))
#else
# define cpu_to_le32(x) (x)
# define cpu_to_le16(x) (x)
# define le32_to_cpu(x) (x)
# define le16_to_cpu(x) (x)
#endif

static void
wav_init_header(WAVEHDR *fileheader, struct ng_audio_fmt *audio)
{
    /* stolen from cdda2wav */
    int nBitsPerSample = ng_afmt_to_bits[audio->fmtid];
    int channels       = ng_afmt_to_channels[audio->fmtid];
    int rate           = audio->rate;

    unsigned long nBlockAlign = channels * ((nBitsPerSample + 7) / 8);
    unsigned long nAvgBytesPerSec = nBlockAlign * rate;
    unsigned long temp = /* data length */ 0 +
	sizeof(WAVEHDR) - sizeof(CHUNKHDR);

    fileheader->chkRiff.ckid    = cpu_to_le32(FOURCC_RIFF);
    fileheader->fccWave         = cpu_to_le32(FOURCC_WAVE);
    fileheader->chkFmt.ckid     = cpu_to_le32(FOURCC_FMT);
    fileheader->chkFmt.dwSize   = cpu_to_le32(16);
    fileheader->wFormatTag      = cpu_to_le16(WAVE_FORMAT_PCM);
    fileheader->nChannels       = cpu_to_le16(channels);
    fileheader->nSamplesPerSec  = cpu_to_le32(rate);
    fileheader->nAvgBytesPerSec = cpu_to_le32(nAvgBytesPerSec);
    fileheader->nBlockAlign     = cpu_to_le16(nBlockAlign);
    fileheader->wBitsPerSample  = cpu_to_le16(nBitsPerSample);
    fileheader->chkData.ckid    = cpu_to_le32(FOURCC_DATA);
    fileheader->chkRiff.dwSize  = cpu_to_le32(temp);
    fileheader->chkData.dwSize  = cpu_to_le32(0 /* data length */);
}

static void
wav_start_write(int fd, WAVEHDR *fileheader, struct ng_audio_fmt *audio)
{
    wav_init_header(fileheader,audio);
    write(fd,fileheader,sizeof(WAVEHDR));
}

static void
wav_stop_write(int fd, WAVEHDR *fileheader, int wav_size)
{
    unsigned long temp = wav_size + sizeof(WAVEHDR) - sizeof(CHUNKHDR);

    fileheader->chkRiff.dwSize = cpu_to_le32(temp);
    fileheader->chkData.dwSize = cpu_to_le32(wav_size);
    lseek(fd,0,SEEK_SET);
    write(fd,fileheader,sizeof(WAVEHDR));
}

/* ---------------------------------------------------------------------- */

struct files_handle {
    /* file name */
    char   file[MAXPATHLEN];

    /* format */
    struct ng_video_fmt video;
    struct ng_audio_fmt audio;

    /* *.wav file */
    int      wav_fd;
    WAVEHDR  wav_header;
    int      wav_size;

    /* misc */
    int gotcha;
};

static void*
files_open(char *filesname, char *audioname,
	   struct ng_video_fmt *video, const void *priv_video, int fps,
	   struct ng_audio_fmt *audio, const void *priv_audio)
{
    struct files_handle *h;

    if (NULL == (h = malloc(sizeof(*h))))
	return NULL;

    /* init */
    memset(h,0,sizeof(*h));
    h->video         = *video;
    h->audio         = *audio;
    strcpy(h->file,filesname);

    if (h->audio.fmtid != AUDIO_NONE) {
	h->wav_fd = open(audioname, O_CREAT | O_RDWR | O_TRUNC, 0666);
	wav_start_write(h->wav_fd,&h->wav_header,&h->audio);
    }

    return h;
}

static int
files_video(void *handle, struct ng_video_buf *buf)
{
    struct files_handle *h = handle;
    int rc = -1;
    FILE *fp;

    if (h->gotcha) {
	fprintf(stderr,"Oops: can't count up file names any more\n");
	return -1;
    }
    
    switch (h->video.fmtid) {
    case VIDEO_RGB24:
	rc = write_ppm(h->file, buf->data, h->video.width, h->video.height);
	break;
    case VIDEO_GRAY:
	rc = write_pgm(h->file, buf->data, h->video.width, h->video.height);
	break;
    case VIDEO_MJPEG:
	if (NULL == (fp = fopen(h->file,"w"))) {
	    fprintf(stderr,"grab: can't open %s: %s\n",h->file,strerror(errno));
	    rc = -1;
	} else {
	    fwrite(buf->data,buf->size,1,fp);
	    fclose(fp);
	    rc = 0;
	}
    }
    if (1 != patch_up(h->file))
	h->gotcha = 1;
    return rc;
}

static int
files_audio(void *handle, struct ng_audio_buf *buf)
{
    struct files_handle *h = handle;
    if (buf->size != write(h->wav_fd,buf->data,buf->size))
	return -1;
    h->wav_size += buf->size;
    return 0;
}

static int
files_close(void *handle)
{
    struct files_handle *h = handle;

    if (h->audio.fmtid != AUDIO_NONE) {
	wav_stop_write(h->wav_fd,&h->wav_header,h->wav_size);
	close(h->wav_fd);
    }
    free(h);
    return 0;
}

/* ----------------------------------------------------------------------- */
/* data structures describing our capabilities                             */

static const struct ng_format_list files_vformats[] = {
    {
	name:  "ppm",
	ext:   "ppm",
	fmtid: VIDEO_RGB24,
    },{
	name:  "pgm",
	ext:   "pgm",
	fmtid: VIDEO_GRAY,
    },{
	name:  "jpeg",
	ext:   "jpeg",
	fmtid: VIDEO_MJPEG,
    },{
	/* EOF */
    }
};

static const struct ng_format_list wav_aformats[] = {
    {
	name:  "mono8",
	ext:   "wav",
	fmtid: AUDIO_U8_MONO,
    },{
	name:  "mono16",
	ext:   "wav",
	fmtid: AUDIO_S16_LE_MONO,
    },{
	name:  "stereo",
	ext:   "wav",
	fmtid: AUDIO_S16_LE_STEREO,
    },{
	/* EOF */
    }
};

const struct ng_writer files_writer = {
    name:      "files",
    desc:      "multiple image files",
    video:     files_vformats,
    audio:     wav_aformats,
    wr_open:   files_open,
    wr_video:  files_video,
    wr_audio:  files_audio,
    wr_close:  files_close,
};
