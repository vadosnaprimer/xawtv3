#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <jpeglib.h>

#include "mjpeg.h"
#include "colorspace.h"

/* ---------------------------------------------------------------------- */

extern int debug,mjpeg_quality;

static struct jpeg_compress_struct  mjpg_cinfo;
static struct jpeg_error_mgr        mjpg_jerr;
static struct jpeg_destination_mgr  mjpg_dest;

static JOCTET *mjpg_buffer;
static size_t  mjpg_bufsize;
static size_t  mjpg_bufused;
static int     mjpg_tables;

/* ---------------------------------------------------------------------- */

static void mjpg_dest_init(struct jpeg_compress_struct *cinfo)
{
    cinfo->dest->next_output_byte = mjpg_buffer;
    cinfo->dest->free_in_buffer   = mjpg_bufsize;
}

static boolean mjpg_dest_flush(struct jpeg_compress_struct *cinfo)
{
    fprintf(stderr,"mjpg: panic: output buffer too small\n");
    exit(1);
}

static void mjpg_dest_term(struct jpeg_compress_struct *cinfo)
{
    mjpg_bufused = mjpg_bufsize - cinfo->dest->free_in_buffer;
}

/* ---------------------------------------------------------------------- */

static void
mjpg_init(int width, int height)
{
    memset(&mjpg_cinfo,0,sizeof(mjpg_cinfo));
    memset(&mjpg_jerr,0,sizeof(mjpg_jerr));
    mjpg_cinfo.err = jpeg_std_error(&mjpg_jerr);
    jpeg_create_compress(&mjpg_cinfo);

    mjpg_dest.init_destination    = mjpg_dest_init;
    mjpg_dest.empty_output_buffer = mjpg_dest_flush;
    mjpg_dest.term_destination    = mjpg_dest_term;
    mjpg_cinfo.dest               = &mjpg_dest;

    mjpg_cinfo.image_width  = width;
    mjpg_cinfo.image_height = height;
    mjpg_tables = TRUE;
}

void
mjpg_cleanup(void)
{
    if (debug > 1)
	fprintf(stderr,"mjpg_cleanup\n");
    
    jpeg_destroy_compress(&(mjpg_cinfo));
}

/* ---------------------------------------------------------------------- */

void
mjpg_rgb_init(int width, int height)
{
    if (debug > 1)
	fprintf(stderr,"mjpg_rgb_init\n");

    mjpg_init(width, height);

    mjpg_cinfo.input_components = 3;
    mjpg_cinfo.in_color_space = JCS_RGB;

    jpeg_set_defaults(&mjpg_cinfo);
    mjpg_cinfo.dct_method = JDCT_FASTEST;
    jpeg_set_quality(&mjpg_cinfo, mjpeg_quality, TRUE);
    jpeg_suppress_tables(&mjpg_cinfo, TRUE);
}

int
mjpg_rgb_compress(unsigned char *d, unsigned char *s, int p)
{
    int i;
    unsigned char *line;

    if (debug > 1)
	fprintf(stderr,"mjpg_rgb_compress\n");
    
    mjpg_buffer  = d;
    mjpg_bufsize = 3*mjpg_cinfo.image_width*mjpg_cinfo.image_height;

    jpeg_start_compress(&mjpg_cinfo, mjpg_tables);
    for (i = 0, line = s; i < mjpg_cinfo.image_height;
	 i++, line += 3*mjpg_cinfo.image_width)
	jpeg_write_scanlines(&mjpg_cinfo, &line, 1);
    jpeg_finish_compress(&(mjpg_cinfo));
//    mjpg_tables = FALSE;

    return mjpg_bufused;
}

int
mjpg_bgr_compress(unsigned char *d, unsigned char *s, int p)
{
    swap_rgb24(s,p);
    return mjpg_rgb_compress(d,s,p);
}

/* ---------------------------------------------------------------------- */

static int rwidth,rheight;
unsigned char **mjpg_ptrs[3];
unsigned char **mjpg_run[3];

static void
mjpg_yuv_init(int width, int height, int format)
{
    if (debug > 1)
	fprintf(stderr,"mjpg_yuv_init\n");

    /* save real size */
    rwidth  = width;
    rheight = height;

    /* fix size to match DCT blocks (I'm not going to copy around
       data to pad stuff, so we'll simplay cut off edges) */
    width  &= ~(2*DCTSIZE-1);
    height &= ~(2*DCTSIZE-1);
    mjpg_init(width, height);

    mjpg_cinfo.input_components = 3;
    mjpg_cinfo.in_color_space = JCS_YCbCr; 

    jpeg_set_defaults(&mjpg_cinfo);
    mjpg_cinfo.dct_method = JDCT_FASTEST;
    jpeg_set_quality(&mjpg_cinfo, mjpeg_quality, TRUE);

    mjpg_cinfo.raw_data_in = TRUE;
    jpeg_set_colorspace(&mjpg_cinfo,JCS_YCbCr);


    if (format == 420) {
	mjpg_cinfo.comp_info[0].h_samp_factor = 2;
	mjpg_cinfo.comp_info[0].v_samp_factor = 2;
	mjpg_ptrs[0] = malloc(height*sizeof(char*));
	
	mjpg_cinfo.comp_info[1].h_samp_factor = 1;
	mjpg_cinfo.comp_info[1].v_samp_factor = 1;
	mjpg_ptrs[1] = malloc(height*sizeof(char*)/2);
	
	mjpg_cinfo.comp_info[2].h_samp_factor = 1;
	mjpg_cinfo.comp_info[2].v_samp_factor = 1;
	mjpg_ptrs[2] = malloc(height*sizeof(char*)/2);
    }
    if (format == 422) {
	mjpg_cinfo.comp_info[0].h_samp_factor = 2;
	mjpg_cinfo.comp_info[0].v_samp_factor = 1;
	mjpg_ptrs[0] = malloc(height*sizeof(char*));
	
	mjpg_cinfo.comp_info[1].h_samp_factor = 1;
	mjpg_cinfo.comp_info[1].v_samp_factor = 1;
	mjpg_ptrs[1] = malloc(height*sizeof(char*));
	
	mjpg_cinfo.comp_info[2].h_samp_factor = 1;
	mjpg_cinfo.comp_info[2].v_samp_factor = 1;
	mjpg_ptrs[2] = malloc(height*sizeof(char*));
    }

    jpeg_suppress_tables(&mjpg_cinfo, TRUE);
}

void
mjpg_420_init(int width, int height)
{
    mjpg_yuv_init(width, height, 420);
}

static int
mjpg_420_compress(void)
{
    int y;

    mjpg_run[0] = mjpg_ptrs[0];
    mjpg_run[1] = mjpg_ptrs[1];
    mjpg_run[2] = mjpg_ptrs[2];
    
    jpeg_start_compress(&mjpg_cinfo, mjpg_tables);
    for (y = 0; y < mjpg_cinfo.image_height; y += 2*DCTSIZE) {
	jpeg_write_raw_data(&mjpg_cinfo, mjpg_run,2*DCTSIZE);
	mjpg_run[0] += 2*DCTSIZE;
	mjpg_run[1] += DCTSIZE;
	mjpg_run[2] += DCTSIZE;
    }
    jpeg_finish_compress(&(mjpg_cinfo));
    
    return mjpg_bufused;
}

int
mjpg_422_420_compress(unsigned char *d, unsigned char *s, int p)
{
    unsigned char *line;
    int i;

    if (debug > 1)
	fprintf(stderr,"mjpg_422_420_compress\n");

    mjpg_buffer  = d;
    mjpg_bufsize = 3*mjpg_cinfo.image_width*mjpg_cinfo.image_height;

    line = s;
    for (i = 0; i < mjpg_cinfo.image_height; i++, line += rwidth)
	mjpg_ptrs[0][i] = line;

    line = s + rwidth*rheight;
    for (i = 0; i < mjpg_cinfo.image_height; i+=2, line += rwidth)
	mjpg_ptrs[1][i/2] = line;

    line = s + rwidth*rheight*3/2;
    for (i = 0; i < mjpg_cinfo.image_height; i+=2, line += rwidth)
	mjpg_ptrs[2][i/2] = line;

    return mjpg_420_compress();
}

int
mjpg_420_420_compress(unsigned char *d, unsigned char *s, int p)
{
    unsigned char *line;
    int i;

    if (debug > 1)
	fprintf(stderr,"mjpg_420_420_compress\n");

    mjpg_buffer  = d;
    mjpg_bufsize = 3*mjpg_cinfo.image_width*mjpg_cinfo.image_height;

    line = s;
    for (i = 0; i < mjpg_cinfo.image_height; i++, line += rwidth)
	mjpg_ptrs[0][i] = line;

    line = s + rwidth*rheight;
    for (i = 0; i < mjpg_cinfo.image_height; i+=2, line += rwidth/2)
	mjpg_ptrs[1][i/2] = line;

    line = s + rwidth*rheight*5/4;
    for (i = 0; i < mjpg_cinfo.image_height; i+=2, line += rwidth/2)
	mjpg_ptrs[2][i/2] = line;

    return mjpg_420_compress();
}

/* ---------------------------------------------------------------------- */

void
mjpg_422_init(int width, int height)
{
    mjpg_yuv_init(width, height, 422);
}

static int
mjpg_422_compress(void)
{
    int y;

    mjpg_run[0] = mjpg_ptrs[0];
    mjpg_run[1] = mjpg_ptrs[1];
    mjpg_run[2] = mjpg_ptrs[2];
    
    mjpg_cinfo.write_JFIF_header = FALSE;
    jpeg_start_compress(&mjpg_cinfo, mjpg_tables);
    jpeg_write_marker(&mjpg_cinfo, JPEG_APP0, "AVI1\0\0\0\0", 8);
    for (y = 0; y < mjpg_cinfo.image_height; y += DCTSIZE) {
	jpeg_write_raw_data(&mjpg_cinfo, mjpg_run, DCTSIZE);
	mjpg_run[0] += DCTSIZE;
	mjpg_run[1] += DCTSIZE;
	mjpg_run[2] += DCTSIZE;
    }
    jpeg_finish_compress(&(mjpg_cinfo));
//    mjpg_tables = FALSE;
    
    return mjpg_bufused;
}

int
mjpg_422_422_compress(unsigned char *d, unsigned char *s, int p)
{
    unsigned char *line;
    int i;

    if (debug > 1)
	fprintf(stderr,"mjpg_422_422_compress\n");

    mjpg_buffer  = d;
    mjpg_bufsize = 3*mjpg_cinfo.image_width*mjpg_cinfo.image_height;

    line = s;
    for (i = 0; i < mjpg_cinfo.image_height; i++, line += rwidth)
	mjpg_ptrs[0][i] = line;

    line = s + rwidth*rheight;
    for (i = 0; i < mjpg_cinfo.image_height; i++, line += rwidth/2)
	mjpg_ptrs[1][i] = line;

    line = s + rwidth*rheight*3/2;
    for (i = 0; i < mjpg_cinfo.image_height; i++, line += rwidth/2)
	mjpg_ptrs[2][i] = line;

    return mjpg_422_compress();
}
