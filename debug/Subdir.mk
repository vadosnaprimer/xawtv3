
# variables
TARGETS-debug :=
ifeq ($(FOUND_ALSA),yes)
TARGETS-debug += \
	debug/hwscan
endif
ifeq ($(FOUND_X11),yes)
TARGETS-debug += \
	debug/xvideo
endif

debug/hwscan: \
	debug/hwscan.o \
	libng/libng.a

debug/xvideo: debug/xvideo.o

debug/hwscan : LDLIBS  += $(ALSA_LIBS) -ljpeg
debug/xvideo : LDLIBS  += $(ATHENA_LIBS)

# global targets
all:: $(TARGETS-debug)

distclean::
	rm -f $(TARGETS-debug)

