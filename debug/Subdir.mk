
# variables
TARGETS-debug := \
	debug/xvideo \
	debug/vbi-debug
ifeq ($(FOUND_ALSA),yes)
TARGETS-debug += \
	debug/hwscan
endif
ifeq ($(FOUND_MOTIF)$(FOUND_GL),yesyes)
TARGETS-debug += \
	debug/gl
endif

OBJS-vbi-debug := \
	debug/vbi-debug.o \
	libvbi/libvbi.a
LIBS-vbi-debug := 

OBJS-gl := \
	debug/gl.o \
	common/RegEdit.o \
	libng/libng.a
LIBS-gl := $(THREADLIB) $(MOTIF_LIBS) -lGLU -lGL -ljpeg -lm

OBJS-hwscan := \
	debug/hwscan.o \
	libng/libng.a
LIBS-hwscan := $(ALSA_LIBS) -ljpeg

# local targets
debug/xvideo: debug/xvideo.o
	$(CC) $(CFLAGS) -o $@  $< $(ATHENA_LIBS)

debug/hwscan: $(OBJS-hwscan)
	$(CC) $(CFLAGS) -o $@  $(OBJS-hwscan) $(LIBS-hwscan) $(DLFLAGS)

debug/vbi-debug: $(OBJS-vbi-debug)
	$(CC) $(CFLAGS) -o $@  $(OBJS-vbi-debug) $(LIBS-vbi-debug) $(DLFLAGS)

debug/gl: $(OBJS-gl)
	$(CC) $(CFLAGS) -o $@  $(OBJS-gl) $(LIBS-gl) $(DLFLAGS)

# global targets
all:: $(TARGETS-debug)

distclean::
	rm -f $(TARGETS-debug)
