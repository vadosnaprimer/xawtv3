
# variables
TARGETS-console := \
	console/dump-mixers \
	console/record \
	console/scantv \
	console/showriff \
	console/streamer \
	console/webcam
TARGETS-v4l-conf := 

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

OBJS-fbtv := \
	console/fbtv.o \
	console/fbtools.o \
	console/fs.o \
	console/matrox.o \
	common/channel-no-x11.o \
	$(OBJS-common-input) \
	$(OBJS-common-capture)
LIBS-fbtv := \
	$(THREAD_LIBS) $(CURSES_LIBS) $(LIRC_LIBS) $(ALSA_LIBS) \
	$(FS_LIBS) -ljpeg -lm

OBJS-ttv := \
	console/aa.o \
	common/channel-no-x11.o \
	$(OBJS-common-capture)
LIBS-ttv := \
	$(THREAD_LIBS) $(AA_LIBS) -ljpeg -lm

OBJS-scantv := \
	console/scantv.o \
	common/channel-no-x11.o \
	$(OBJS-common-capture) \
	libvbi/libvbi.a
OBJS-streamer := \
	console/streamer.o \
	common/channel-no-x11.o \
	$(OBJS-common-capture)
LIBS-capture := \
	$(THREAD_LIBS) -ljpeg -lm

OBJS-webcam := \
	console/webcam.o \
	console/ftp.o \
	common/parseconfig.o \
	libng/libng.a
LIBS-webcam := \
	$(THREAD_LIBS) -ljpeg

# local targets
console/dump-mixers: console/dump-mixers.o
console/showriff: console/showriff.o

console/radio: console/radio.o
	$(CC) $(CFLAGS) -o $@  $< $(CURSES_LIBS)

console/record: console/record.o
	$(CC) $(CFLAGS) -o $@  $< $(CURSES_LIBS)

console/v4l-conf: console/v4l-conf.o
	$(CC) $(CFLAGS) -o $@  $< $(ATHENA_LIBS)

console/fbtv: $(OBJS-fbtv)
	$(CC) $(CFLAGS) -o $@  $(OBJS-fbtv) $(LIBS-fbtv) $(DLFLAGS)

console/scantv: $(OBJS-scantv)
	$(CC) $(CFLAGS) -o $@  $(OBJS-scantv) $(LIBS-capture) $(DLFLAGS)

console/streamer: $(OBJS-streamer)
	$(CC) $(CFLAGS) -o $@  $(OBJS-streamer) $(LIBS-capture) $(DLFLAGS)

console/webcam: $(OBJS-webcam)
	$(CC) $(CFLAGS) -o $@  $(OBJS-webcam) $(LIBS-webcam) $(DLFLAGS)

console/ttv: $(OBJS-ttv)
	$(CC) $(CFLAGS) -o $@  $(OBJS-ttv) $(LIBS-ttv) $(DLFLAGS)


# global targets
all:: $(TARGETS-console) $(TARGETS-v4l-conf)

install::
	$(INSTALL_PROGRAM) $(TARGETS-console) $(bindir)
ifeq ($(FOUND_OS),linux)
	$(INSTALL_PROGRAM) $(SUID_ROOT) $(TARGETS-v4l-conf) $(bindir)
endif

distclean::
	rm -f $(TARGETS-console) $(TARGETS-v4l-conf)

