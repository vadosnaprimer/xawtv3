#include "config.h"

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifdef HAVE_LINUX_JOYSTICK_H
# include <linux/joystick.h>
#endif

#include "grab-ng.h"
#include "commands.h"
#include "joystick.h"

/*-----------------------------------------------------------------------*/

extern int debug;

#ifdef HAVE_LINUX_JOYSTICK_H
struct JOYTAB {
    int class;
    int number;
    int value;
    int argc;
    char *argv[8];
};

static struct JOYTAB joytab[] = {
    { JS_EVENT_BUTTON, 0, 1,    1, {"quit"}},
    { JS_EVENT_BUTTON, 1, 1,    1, {"fullscreen"}},
    { JS_EVENT_AXIS, 1, -32767, 2, {"volume", "inc"}},
    { JS_EVENT_AXIS, 1,  32767, 2, {"volume", "dec"}},
    { JS_EVENT_AXIS, 0,  32767, 2, {"setchannel", "next"}},
    { JS_EVENT_AXIS, 0, -32767, 2, {"setchannel", "prev"}},
};

#define NJOYTAB (sizeof(joytab)/sizeof(struct JOYTAB))
#endif

int joystick_tv_init(char *dev)
{
#ifdef HAVE_LINUX_JOYSTICK_H
    int fd;

    if (NULL == dev)
	return -1;
    if (-1 == (fd = open(dev, O_NONBLOCK))) {
	fprintf(stderr, "joystick: open %s: %s\n",dev,strerror(errno));
	return -1;
    }
    fcntl(fd,F_SETFD,FD_CLOEXEC);
    return fd;
#else
    return -1;
#endif
}

void joystick_tv_havedata(int js)
{
#ifdef HAVE_LINUX_JOYSTICK_H
    int i;
    struct js_event event;
    if (debug)
	fprintf(stderr, "joystick: received input\n");
    if (read(js, &event, sizeof(struct js_event))) {
	for (i = 0; i < NJOYTAB; i++)
	    if (joytab[i].class == (event.type)
		&& joytab[i].number == event.number
		&& joytab[i].value == event.value)
		break;
	if (i != NJOYTAB)
	    do_command(joytab[i].argc, joytab[i].argv);
    }
#endif
}
