#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#include "config.h"

#ifdef HAVE_LIBLIRC_CLIENT
# include <lirc/lirc_client.h>
#endif
#include "lirc.h"
#include "commands.h"

/*-----------------------------------------------------------------------*/

extern int debug;

#ifdef HAVE_LIBLIRC_CLIENT
static struct lirc_config *config;
#endif

int lirc_tv_init()
{
#ifdef HAVE_LIBLIRC_CLIENT
    int fd;
    
    if (-1 == (fd = lirc_init("xawtv",debug))) {
	fprintf(stderr,"no infrared remote support available\n");
	return -1;
    }
    
    if (0 != lirc_readconfig(NULL,&config,NULL)) {
	lirc_deinit();
	return -1;
    }
    fcntl(fd,F_SETFL,O_NONBLOCK);
    fcntl(fd,F_SETFD,FD_CLOEXEC);
    
    return fd;
#else
    return -1;
#endif
}

int lirc_tv_havedata()
{
#ifdef HAVE_LIBLIRC_CLIENT
    char *code,*cmd,**argv;
    int argc;
    int ret=-1;
    
    while (lirc_nextcode(&code)==0 && code!=NULL) {
	ret = 0;
	while (lirc_code2char(config,code,&cmd)==0 && cmd!=NULL) {
	    if (debug)
		fprintf(stderr,"lirc: \"%s\"\n", cmd);
	    argv = split_cmdline(cmd,&argc);
	    do_command(argc,argv);
	}
	free(code);
    }
    return ret;
#else
    return 0;
#endif
}
