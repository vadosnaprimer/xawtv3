/*  SHOWRIFF.c
 *  Extracts some infos from RIFF files
 *  (c)94 UP-Vision Computergrafik for c't
 *  Written in ANSI-C. No special header files needed to compile.
 *
 *  modified by Gerd Knorr:
 *   - dos2unix :-)
 *   - fixed warnings
 *   - added some tags (checked xanim sources for info)
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef unsigned long DWORD;
typedef unsigned short WORD;
typedef DWORD FOURCC;             /* Type of FOUR Character Codes */
typedef unsigned char boolean;
#define TRUE  1
#define FALSE 0

/* Macro to convert expressions of form 'F','O','U','R' to
   numbers of type FOURCC: */

#define MAKEFOURCC(a,b,c,d) ( ((DWORD)a)      | (((DWORD)b)<< 8) | \
                             (((DWORD)c)<<16) | (((DWORD)d)<<24)  )

/* The only FOURCCs interpreted by this program: */

#define RIFFtag MAKEFOURCC('R','I','F','F')
#define LISTtag MAKEFOURCC('L','I','S','T')
#define avihtag MAKEFOURCC('a','v','i','h')
#define strhtag MAKEFOURCC('s','t','r','h')
#define strftag MAKEFOURCC('s','t','r','f')
#define vidstag MAKEFOURCC('v','i','d','s')
#define audstag MAKEFOURCC('a','u','d','s')

/* Build a string from a FOURCC number
   (s must have room for at least 5 chars) */

void FOURCC2Str(FOURCC fcc, char* s)
{
  s[0]=(fcc      ) & 0xFF;
  s[1]=(fcc >>  8) & 0xFF;
  s[2]=(fcc >> 16) & 0xFF;
  s[3]=(fcc >> 24) & 0xFF;
  s[4]=0;
}

static DWORD fcc_type;

#define EoLST 0
#define INT32 1
#define INT16 2
#define FLAGS 3

struct VAL {
    int  type;
    char *name;
};

struct VAL names_avih[] = {
    { INT32, "us_frame" },
    { INT32, "max_bps" },
    { INT32, "pad_gran" },
    { FLAGS, "flags" },
    { INT32, "tot_frames" },
    { INT32, "init_frames" },
    { INT32, "streams" },
    { INT32, "sug_bsize" },
    { INT32, "width" },
    { INT32, "height" },
    { INT32, "scale" },
    { INT32, "rate" },
    { INT32, "start" },
    { INT32, "length" },
    { EoLST, NULL }
};

struct VAL names_strh[] = {
    { INT32, "fcc_handler" },
    { FLAGS, "flags" },
    { INT32, "priority" },
    { INT32, "init_frames" },
    { INT32, "scale" },
    { INT32, "rate" },
    { INT32, "start" },
    { INT32, "length" },
    { INT32, "sug_bsize" },
    { INT32, "quality" },
    { INT32, "samp_size" },
    { EoLST, NULL }
};

struct VAL names_strf_vids[] = {
    { INT32, "size" },
    { INT32, "width" },
    { INT32, "height" },
    { INT16, "planes" },
    { INT16, "bit_cnt" },
    { INT32, "compression" },
    { INT32, "image_size" },
    { INT32, "xpels_meter" },
    { INT32, "ypels_meter" },
    { INT32, "num_colors" },
    { INT32, "imp_colors" },
    { EoLST, NULL }
};

struct VAL names_strf_auds[] = {
    { INT16, "format" },
    { INT16, "channels" },
    { INT32, "rate" },
    { INT32, "av_bps" },
    { INT16, "blockalign" },
    { INT16, "size" },
    { EoLST, NULL }    
};

void dump_vals(FILE *f, int count, struct VAL *names)
{
    DWORD i,val32;
    WORD  val16;

    for (i = 0; names[i].type != EoLST; i++) {
	switch (names[i].type) {
	case INT32:
	    fread(&val32,4,1,f);
	    printf("%s=%ld ",names[i].name,val32);
	    break;
	case FLAGS:
	    fread(&val32,4,1,f);
	    printf("%s=0x%lx ",names[i].name,val32);
	    break;
	case INT16:
	    fread(&val16,2,1,f);
	    printf("%s=%ld ",names[i].name,(long)val16);
	    break;
	}
    }
    printf("\n");
}

/* Reads a chunk ID and the chunk's size from file f at actual
   file position : */

boolean ReadChunkHead(FILE* f, FOURCC* ID, DWORD* size)
{
  if (!fread(ID,sizeof(FOURCC),1,f)) return(FALSE);
  if (!fread(size,sizeof(DWORD),1,f)) return(FALSE);
  return(TRUE);
}

/* Processing of a chunk. (Will be called recursively!).
   Processes file f starting at position filepos.
   f contains filesize bytes.
   If DesiredTag!=0, ProcessChunk tests, whether the chunk begins
   with the DesiredTag. If the read chunk is not identical to
   DesiredTag, an error message is printed.
   RekDepth determines the recursion depth of the chunk.
   chunksize is set to the length of the chunk's data (excluding
   header and padding byte).
   ProcessChunk prints out information of the chunk to stdout
   and returns FALSE, if an error occured. */

boolean ProcessChunk(FILE* f, long filepos, long filesize,
                     FOURCC DesiredTag, int RekDepth,
                     DWORD* chunksize)
{
  char   tagstr[5];          /* FOURCC of chunk converted to string */
  FOURCC chunkid;            /* read FOURCC of chunk                */
  long   datapos;            /* position of data in file to process */

  if (filepos>filesize-1) {  /* Oops. Must be something wrong!      */
    printf("\n\n *** Error: Data would be behind end of file!\n");
    return(FALSE);
  }
  fseek(f,filepos,SEEK_SET);    /* Go to desired file position!     */

  if (!ReadChunkHead(f,&chunkid,chunksize)) {  /* read chunk header */
    printf("\n\n *** Error reading chunk at filepos 0x%lx\n",filepos);
    return(FALSE);
  }
  FOURCC2Str(chunkid,tagstr);       /* now we can PRINT the chunkid */
  if (DesiredTag) {                 /* do we have to test identity? */
    if (DesiredTag!=chunkid) {
      char ds[5];
      FOURCC2Str(DesiredTag,ds);
      printf("\n\n *** Error: Expected chunk '%s', found '%s'\n",
             ds,tagstr);
      return(FALSE);
    }
  }

  datapos=filepos+sizeof(FOURCC)+sizeof(DWORD); /* here is the data */

  if (datapos + ((*chunksize+1)&~1) > filesize) {      /* too long? */
    printf("\n\n *** Error: Chunk exceeds file (starts at 0x%lx)\n",
           filepos);
    return(FALSE);
  }

  /* Chunk seems to be ok, print out header: */
  printf("(0x%08lx) %*c  ID:<%s>   Size:0x%08lx\n",
         filepos,(RekDepth+1)*2,' ',tagstr,*chunksize);

  switch (chunkid) {

  /* Depending on the ID of the chunk and the internal state, the
     different IDs can be interpreted. At the moment the only
     interpreted chunks are RIFF- and LIST-chunks. For all other
     chunks only their header is printed out. */

    case RIFFtag:
    case LISTtag: {

      DWORD datashowed;
      FOURCC formtype;       /* format of chunk                     */
      char   formstr[5];     /* format of chunk converted to string */
      DWORD  subchunksize;   /* size of a read subchunk             */

      fread(&formtype,sizeof(FOURCC),1,f);    /* read the form type */
      FOURCC2Str(formtype,formstr);           /* make it printable  */

      /* print out the indented form of the chunk: */
      if (chunkid==RIFFtag)
        printf("%12c %*c  Form Type = <%s>\n",
               ' ',(RekDepth+1)*2,' ',formstr);
      else
        printf("%12c %*c  List Type = <%s>\n",
               ' ',(RekDepth+1)*2,' ',formstr);

      datashowed=sizeof(FOURCC);    /* we showed the form type      */
      datapos+=datashowed;          /* for the rest of the routine  */

      while (datashowed<*chunksize) {      /* while not showed all: */

        long subchunklen;           /* complete size of a subchunk  */

        /* recurse for subchunks of RIFF and LIST chunks: */
        if (!ProcessChunk(f,datapos,filesize,0,
                          RekDepth+1,&subchunksize)) return(FALSE);

        subchunklen = sizeof(FOURCC) +  /* this is the complete..   */
                      sizeof(DWORD)  +  /* .. size of the subchunk  */
                      ((subchunksize+1) & ~1);

        datashowed += subchunklen;      /* we showed the subchunk   */
        datapos    += subchunklen;      /* for the rest of the loop */
      }
    } break;

    /* Feel free to put your extensions here! */

  case avihtag:
      dump_vals(f,sizeof(names_avih)/sizeof(struct VAL),names_avih);
      break;
  case strhtag:
      {
	  char   typestr[5];
	  fread(&fcc_type,sizeof(FOURCC),1,f);
	  FOURCC2Str(fcc_type,typestr);
	  printf("fcc_type=%s  ",typestr);
	  dump_vals(f,sizeof(names_strh)/sizeof(struct VAL),names_strh);
	  break;
      }
  case strftag:
      switch (fcc_type) {
      case vidstag:
	  dump_vals(f,sizeof(names_strf_vids)/sizeof(struct VAL),names_strf_vids);
	  break;
      case audstag:
	  dump_vals(f,sizeof(names_strf_auds)/sizeof(char*),names_strf_auds);
	  break;
      default:
	  printf("unknown\n");
	  break;
      }
      break;
  }

  return(TRUE);
}

int main (int argc, char **argv)
{
  FILE*  f;            /* the input file              */
  long   filesize;     /* its size                    */
  DWORD  chunksize;    /* size of the RIFF chunk data */

  printf("%s shows contents of RIFF files (AVI,WAVE...).\n",argv[0]);
  printf("(c) 1994 UP-Vision Computergrafik for c't\n");
  printf("unix port and some extentions (for avi) by Gerd Knorr\n\n");

  if (argc!=2) { printf("Usage: %s filename\n",argv[0]); return(1); }

  if (!(f=fopen(argv[1],"rb"))) {
    printf("\n\n *** Error opening file %s. Program aborted!\n",
           argv[1]);
    return(1);
  }

  fseek(f, 0, SEEK_END);
  filesize = ftell(f);
  fseek(f, 0, SEEK_SET);

  printf("Contents of file %s (%ld bytes):\n\n",argv[1],filesize);

  /* Process the main chunk, which MUST be a RIFF chunk. */
  if (!ProcessChunk(f,0,filesize,RIFFtag,0,&chunksize)) return(1);

  if (filesize != sizeof(FOURCC)+sizeof(DWORD)+ ((chunksize+1)&~1))
    printf("\n\n *** Warning: Padding bytes after RIFF chunk!\n");

  return(0);
}
