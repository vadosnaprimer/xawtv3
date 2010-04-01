

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

# RegEdit.c is good old K&R ...
common/RegEdit.o : CFLAGS += -Wno-missing-prototypes -Wno-strict-prototypes
common/channel-no-x11.o: CFLAGS += -DNO_X11=1 

common/channel-no-x11.o:: common/channel.c
	@$(echo_compile_c)
	@$(compile_c)
	@$(fixup_deps)
