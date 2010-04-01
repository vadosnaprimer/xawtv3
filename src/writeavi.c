#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <endian.h>

#include "grab.h"
#include "writeavi.h"
#include "colorspace.h"

/*
 * M$ vidcap avi video+audio layout
 *
 * riff avi
 *   list hdrl       header
 *     avih          avi header
 *     list strl     video stream header
 *       strh         
 *       strf        
 *     list strl     audio stream header
 *       strh        
 *       strf        
 *     istf          ??? software
 *     idit          ??? timestamp
 *   yunk            ??? 4k page pad
 *   list movi       data
 *     00db          video data
 *     yunk          ??? 4k page pad
 *     [ ... ]
 *     01wb          audio data
 *     [ ... ]
 *   idx1            video frame index
 *
 */

/* ----------------------------------------------------------------------- */

#define TRAP(txt) fprintf(stderr,"%s:%d:%s\n",__FILE__,__LINE__,txt);exit(1);
#define PERROR(action,file) \
  {fprintf(stderr,"%s: %s: %s",action,file,strerror(errno));return -1;}

#define WAVE_FORMAT_PCM                 (0x0001)
#define WAVE_FORMAT_ALAW                (0x0006)
#define WAVE_FORMAT_MULAW               (0x0007)

typedef unsigned int   uint32;
typedef unsigned short uint16;

struct RIFF_avih {
    uint32 us_frame;          /* microsec per frame */
    uint32 bps;               /* byte/s overall */
    uint32 unknown1;          /* pad_gran (???) */
    uint32 flags;
    uint32 frames;            /* # of frames (all) */
    uint32 init_frames;       /* initial frames (???) */
    uint32 streams;
    uint32 bufsize;           /* suggested buffer size */
    uint32 width;
    uint32 height;
    uint32 scale;
    uint32 rate;
    uint32 start;
    uint32 length;
};

struct RIFF_strh {
    char   type[4];           /* stream type */
    char   handler[4];
    uint32 flags;
    uint32 priority;
    uint32 init_frames;       /* initial frames (???) */
    uint32 scale;
    uint32 rate;
    uint32 start;
    uint32 length;
    uint32 bufsize;           /* suggested buffer size */
    uint32 quality;
    uint32 samplesize;
    /* XXX 16 bytes ? */
};

struct RIFF_strf_vids {       /* == BitMapInfoHeader */
    uint32 size;
    uint32 width;
    uint32 height;
    uint16 planes;
    uint16 bit_cnt;
    char   compression[4];
    uint32 image_size;
    uint32 xpels_meter;
    uint32 ypels_meter;
    uint32 num_colors;        /* used colors */
    uint32 imp_colors;        /* important colors */
    /* may be more for some codecs */
};

struct RIFF_strf_auds {       /* == WaveHeader (?) */
    uint16 format;
    uint16 channels;
    uint32 rate;
    uint32 av_bps;
    uint16 blockalign;
    uint16 size;
};

#define size_strl_vids (sizeof(struct RIFF_strh) + \
			sizeof(struct RIFF_strf_vids) + \
			4*5)
#define size_strl_auds (sizeof(struct RIFF_strh) + \
			sizeof(struct RIFF_strf_auds) + \
			4*5)

static struct AVI_HDR {
    char                     riff_id[4];
    uint32                   riff_size;
    char                     riff_type[4];

    char                       hdrl_list_id[4];
    uint32                     hdrl_size;
    char                       hdrl_type[4];

    char                         avih_id[4];
    uint32                       avih_size;
    struct RIFF_avih             avih;
} avi_hdr = {
    {'R','I','F','F'}, 0,                             {'A','V','I',' '},
    {'L','I','S','T'}, 0,                             {'h','d','r','l'},
    {'a','v','i','h'}, AVI_SWAP4(sizeof(struct RIFF_avih)),      {}
};

static struct AVI_HDR_VIDEO {
    char                         strl_list_id[4];
    uint32                       strl_size;
    char                         strl_type[4];

    char                           strh_id[4];
    uint32                         strh_size;
    struct RIFF_strh               strh;

    char                           strf_id[4];
    uint32                         strf_size;
    struct RIFF_strf_vids          strf;
} avi_hdr_video = {
    {'L','I','S','T'}, AVI_SWAP4(size_strl_vids),                {'s','t','r','l'},
    {'s','t','r','h'}, AVI_SWAP4(sizeof(struct RIFF_strh)),      {{'v','i','d','s'}},
    {'s','t','r','f'}, AVI_SWAP4(sizeof(struct RIFF_strf_vids)),
    {AVI_SWAP4(sizeof(struct RIFF_strf_vids))}
};

static struct AVI_HDR_AUDIO {
    char                         strl_list_id[4];
    uint32                       strl_size;
    char                         strl_type[4];

    char                           strh_id[4];
    uint32                         strh_size;
    struct RIFF_strh               strh;

    char                           strf_id[4];
    uint32                         strf_size;
    struct RIFF_strf_auds          strf;
} avi_hdr_audio = {
    {'L','I','S','T'}, AVI_SWAP4(size_strl_auds),                {'s','t','r','l'},
    {'s','t','r','h'}, AVI_SWAP4(sizeof(struct RIFF_strh)),      {{'a','u','d','s'}},
    {'s','t','r','f'}, AVI_SWAP4(sizeof(struct RIFF_strf_auds)), {}
};

static struct AVI_DATA {
    char                       data_list_id[4];
    uint32                     data_size;
    char                       data_type[4];

    /* audio+video data follows */
    
} avi_data = {
    {'L','I','S','T'}, 0,                   {'m','o','v','i'},
};

struct CHUNK_HDR {
    char                       id[4];
    uint32                     size;
};

static struct CHUNK_HDR frame_hdr = {
    {'0','0','d','b'}, 0
};
static struct CHUNK_HDR sound_hdr = {
    {'0','1','w','b'}, 0
};
static struct CHUNK_HDR idx_hdr = {
    {'i','d','x','1'}, 0
};

struct IDX_RECORD {
    char                      id[4];
    uint32                    flags;
    uint32                    offset;
    uint32                    size;
};


/* file handle */
static int fd;

/* statistics */
static int hdr_size;
static int frames;
static int audio_size;
static int data_size;
static int idx_size;
static struct MOVIE_PARAMS params;
static char file[256];

/* video stuff */
static int frame_bytes, screen_bytes;
static unsigned char *framebuf;

/* audio stuff */
static int have_audio;

/* frame index */
static struct IDX_RECORD *idx_array;
static int idx_index, idx_count, idx_offset;

/* ----------------------------------------------------------------------- */

int
avi_open(char *filename, struct MOVIE_PARAMS *par)
{
    /* reset */
    hdr_size = frames = audio_size = 0;
    memcpy(&params,par,sizeof(params));
    strcpy(file,filename);
    have_audio = (params.channels > 0);
    memset(&avi_hdr_video.strf,0,sizeof(avi_hdr_video.strf));
    memset(&avi_hdr_audio.strf,0,sizeof(avi_hdr_audio.strf));
    
    switch (params.video_format) {
    case VIDEO_RGB15_LE:
	frame_bytes  = params.width * params.height *2;
	screen_bytes = 2;
	break;
    case VIDEO_BGR24:
	frame_bytes  = params.width * params.height *3;
	screen_bytes = 3;
	break;
    case VIDEO_MJPEG:
	frame_bytes  = params.width * params.height *3;
	screen_bytes = 3;
        avi_hdr_video.strh.handler[0] = 'M';
        avi_hdr_video.strh.handler[1] = 'J';
        avi_hdr_video.strh.handler[2] = 'P';
        avi_hdr_video.strh.handler[3] = 'G';
        avi_hdr_video.strf.compression[0] = 'M';
        avi_hdr_video.strf.compression[1] = 'J';
        avi_hdr_video.strf.compression[2] = 'P';
        avi_hdr_video.strf.compression[3] = 'G';
	break;
    default:
	TRAP("unsupported video format");
    }
    if ((framebuf = malloc(frame_bytes)) == NULL)
	exit(1);

    if (-1 == (fd = open(filename,O_CREAT | O_RDWR | O_TRUNC, 0666)))
	PERROR("open",file);

    /* general */
    avi_hdr.avih.us_frame    = AVI_SWAP4(1000000/params.fps);
    avi_hdr.avih.bps         =
	AVI_SWAP4(frame_bytes * params.fps +
	params.channels * (params.bits/8) * params.rate);
    avi_hdr.avih.streams     = AVI_SWAP4(have_audio ? 2 : 1);
    avi_hdr.avih.width       = AVI_SWAP4(params.width);
    avi_hdr.avih.height      = AVI_SWAP4(params.height);
    hdr_size += write(fd,&avi_hdr,sizeof(avi_hdr));

    /* video */
    frame_hdr.size                = AVI_SWAP4(frame_bytes);
    avi_hdr_video.strh.scale      = AVI_SWAP4(1000000/params.fps);
    avi_hdr_video.strh.rate       = AVI_SWAP4(1000000);
    
    avi_hdr_video.strf.width      = AVI_SWAP4(params.width);
    avi_hdr_video.strf.height     = AVI_SWAP4(params.height);
    avi_hdr_video.strf.planes     = AVI_SWAP2(1);
    avi_hdr_video.strf.bit_cnt    = AVI_SWAP2(frame_bytes/params.width/params.height*8);
    avi_hdr_video.strf.image_size = AVI_SWAP4(frame_bytes);
    hdr_size += write(fd,&avi_hdr_video,sizeof(avi_hdr_video));

    /* audio */
    if (have_audio) {
	avi_hdr_audio.strh.scale      = AVI_SWAP4(params.channels * (params.bits/8));
	avi_hdr_audio.strh.rate       =
	    AVI_SWAP4(params.channels * (params.bits/8) * params.rate);
	avi_hdr_audio.strh.samplesize = AVI_SWAP4(params.channels * (params.bits/8));

	avi_hdr_audio.strf.format     = AVI_SWAP2(WAVE_FORMAT_PCM);
	avi_hdr_audio.strf.channels   = AVI_SWAP2(params.channels);
	avi_hdr_audio.strf.rate       = AVI_SWAP4(params.rate);
	avi_hdr_audio.strf.av_bps     = 
	    AVI_SWAP4(params.channels * (params.bits/8) * params.rate);
	avi_hdr_audio.strf.blockalign = AVI_SWAP2(params.channels * (params.bits/8));
	avi_hdr_audio.strf.size       = AVI_SWAP2(params.bits);
	hdr_size += write(fd,&avi_hdr_audio,sizeof(avi_hdr_audio));
    }

    /* data */
    if (-1 == write(fd,&avi_data,sizeof(avi_data)))
	PERROR("write",file);
    data_size  = 4; /* list type */

    idx_index  = 0;
    idx_offset = hdr_size + sizeof(avi_data);

    return 0;
}

void avi_addindex(char * fourcc,int flags,int chunksize)
{
    if (idx_index == idx_count) {
	idx_count += 256;
	idx_array = realloc(idx_array,idx_count*sizeof(struct IDX_RECORD));
    }
    memcpy(idx_array[idx_index].id,fourcc,4);
    idx_array[idx_index].flags=AVI_SWAP4(flags);
    idx_array[idx_index].offset=AVI_SWAP4(idx_offset-hdr_size-8);
    idx_array[idx_index].size=AVI_SWAP4(chunksize);
    idx_index++;
    idx_offset += chunksize + sizeof(struct CHUNK_HDR);
}

int
avi_writeframe(void *data, int datasize)
{
    unsigned char *d;
    int y,size=0;
    
    switch (params.video_format) {
    case VIDEO_RGB15_LE:
    case VIDEO_BGR24:
	size = frame_bytes;
	frame_hdr.size = AVI_SWAP4(size);
	if (-1 == write(fd,&frame_hdr,sizeof(frame_hdr)))
	    PERROR("write",file);
	for (y = params.height-1; y >= 0; y--) {
	    d = ((unsigned char*)data) + y*params.width * screen_bytes;
	    if (-1 == write(fd,d,screen_bytes*params.width))
		PERROR("write",file);
	}
	break;
    case VIDEO_MJPEG:
	size = (datasize+3) & ~3;
	frame_hdr.size = AVI_SWAP4(size);
	if (-1 == write(fd,&frame_hdr,sizeof(frame_hdr)))
	    PERROR("write",file);
	if (-1 == write(fd,data,size))
	    PERROR("write",file);
	break;
    }
    avi_addindex(frame_hdr.id,0x12,size);
    data_size += size + sizeof(frame_hdr);
    frames    += 1;
    return 0;
}

int
avi_writesound(void *data, int datasize)
{
    sound_hdr.size = AVI_SWAP4(datasize);
    if (-1 == write(fd,&sound_hdr,sizeof(sound_hdr)))
	PERROR("write",file);
    if (-1 == write(fd,data,datasize))
	PERROR("write",file);
    avi_addindex(sound_hdr.id,0x0,datasize);
    data_size  += datasize + sizeof(sound_hdr);
    audio_size += datasize;
    return 0;
}

int
avi_close()
{
    /* write frame index */
    idx_hdr.size= AVI_SWAP4(idx_index * sizeof(struct IDX_RECORD));
    write(fd,&idx_hdr,sizeof(idx_hdr));
    write(fd,idx_array,idx_index*sizeof(struct IDX_RECORD));
    idx_size += idx_index * sizeof(struct IDX_RECORD)
	+ sizeof(struct CHUNK_HDR);
    
    /* fill in some statistic values ... */
    avi_hdr.riff_size         = AVI_SWAP4(hdr_size+data_size+idx_size);
    avi_hdr.hdrl_size         = AVI_SWAP4(hdr_size - 4*5);
    avi_hdr.avih.frames       = AVI_SWAP4(frames);
    avi_hdr_video.strh.length = AVI_SWAP4(frames);
    if (have_audio)
	avi_hdr_audio.strh.length = AVI_SWAP4(audio_size);
    avi_data.data_size        = AVI_SWAP4(data_size);
    
    /* ... and write header again */
    lseek(fd,0,SEEK_SET);
    write(fd,&avi_hdr,sizeof(avi_hdr));
    write(fd,&avi_hdr_video,sizeof(avi_hdr_video));
    if (have_audio)
	write(fd,&avi_hdr_audio,sizeof(avi_hdr_audio));
    write(fd,&avi_data,sizeof(avi_data));

    close(fd);
    free(framebuf);
    return 0;
}
