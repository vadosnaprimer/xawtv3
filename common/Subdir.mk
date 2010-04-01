
OBJS-common-capture := \
	common/sound.o \
	common/webcam.o \
	common/frequencies.o \
	common/commands.o \
	common/parseconfig.o \
	common/capture.o \
	common/event.o \
	libng/libng.a

OBJS-common-input := \
	common/lirc.o \
	common/joystick.o \
	common/midictrl.o

common/channel-no-x11.o:: common/channel.c
	$(CC) $(CFLAGS) -DNO_X11=1 -Wp,-MD,$*.dep -c -o $@ $<
	@sed -e "s|.*\.o:|$@::|" < $*.dep > $*.d && rm -f $*.dep
