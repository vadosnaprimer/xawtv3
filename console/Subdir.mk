
# targets to build
TARGETS-console := \
	console/dump-mixers \
	console/record \
	console/showriff \
	console/streamer \
	console/webcam
TARGETS-v4l-conf := 

ifeq ($(FOUND_ZVBI),yes)
TARGETS-console += \
	console/scantv
endif
ifeq ($(FOUND_AALIB),yes)
TARGETS-console += \
	console/ttv
endif
ifeq ($(FOUND_OS),linux)
TARGETS-console += \
	console/radio \
	console/fbtv
TARGETS-v4l-conf += \
	console/v4l-conf
endif

# objects for targets
console/fbtv: \
	console/fbtv.o \
	console/fbtools.o \
	console/fs.o \
	console/matrox.o \
	common/channel-no-x11.o \
	$(OBJS-common-input) \
	$(OBJS-common-capture)

console/ttv: \
	console/ttv.o \
	common/channel-no-x11.o \
	$(OBJS-common-capture)

console/scantv: \
	console/scantv.o \
	common/vbi-data.o \
	common/channel-no-x11.o \
	$(OBJS-common-capture)

console/streamer: \
	console/streamer.o \
	common/channel-no-x11.o \
	$(OBJS-common-capture)

console/webcam: \
	console/webcam.o \
	console/ftp.o \
	common/parseconfig.o \
	libng/libng.a

console/dump-mixers: console/dump-mixers.o
console/showriff: console/showriff.o
console/radio: console/radio.o
console/record: console/record.o
console/v4l-conf: console/v4l-conf.o

# libraries to link
console/fbtv     : LDLIBS  += \
	$(THREAD_LIBS) $(CURSES_LIBS) $(LIRC_LIBS) $(ALSA_LIBS) \
	$(FS_LIBS) -ljpeg -lm
console/ttv      : LDLIBS  += $(THREAD_LIBS) $(AA_LIBS) -ljpeg -lm
console/scantv   : LDLIBS  += $(THREAD_LIBS) $(VBI_LIBS) -ljpeg
console/streamer : LDLIBS  += $(THREAD_LIBS) -ljpeg -lm
console/webcam   : LDLIBS  += $(THREAD_LIBS) -ljpeg -lm
console/radio    : LDLIBS  += $(CURSES_LIBS)
console/record   : LDLIBS  += $(CURSES_LIBS)
console/v4l-conf : LDLIBS  += $(ATHENA_LIBS)

# linker flags
console/fbtv     : LDFLAGS := $(DLFLAGS)
console/ttv      : LDFLAGS := $(DLFLAGS)
console/scantv   : LDFLAGS := $(DLFLAGS)
console/streamer : LDFLAGS := $(DLFLAGS)
console/webcam   : LDFLAGS := $(DLFLAGS)

# global targets
all:: $(TARGETS-console) $(TARGETS-v4l-conf)

install::
	$(INSTALL_PROGRAM) $(TARGETS-console) $(bindir)
ifeq ($(FOUND_OS),linux)
	$(INSTALL_PROGRAM) $(SUID_ROOT) $(TARGETS-v4l-conf) $(bindir)
endif

distclean::
	rm -f $(TARGETS-console) $(TARGETS-v4l-conf)
