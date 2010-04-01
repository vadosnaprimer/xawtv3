
# variables
TARGETS-debug := \
	debug/xvideo
ifeq ($(FOUND_ALSA),yes)
TARGETS-debug += \
	debug/hwscan
endif
ifeq ($(FOUND_MOTIF)$(FOUND_GL),yesyes)
TARGETS-debug += \
	debug/gl
endif

debug/gl: \
	debug/gl.o \
	common/RegEdit.o \
	libng/libng.a

debug/hwscan: \
	debug/hwscan.o \
	libng/libng.a

debug/xvideo: debug/xvideo.o

debug/gl     : LDLIBS  := $(THREADLIB) $(MOTIF_LIBS) -lGLU -lGL -ljpeg -lm
debug/hwscan : LDLIBS  := $(ALSA_LIBS) -ljpeg
debug/xvideo : LDLIBS  := $(ATHENA_LIBS)

debug/gl     : LDFLAGS := $(DLFLAGS)

# global targets
all:: $(TARGETS-debug)

distclean::
	rm -f $(TARGETS-debug)

