
# targets to build
TARGETS-contrib-plugins := \
	libng/contrib-plugins/flt-smooth.so
ifeq ($(FOUND_ALSA),yes)
TARGETS-contrib-plugins += \
	libng/contrib-plugins/snd-alsa.so
endif

# global targets
all:: $(TARGETS-contrib-plugins)

install::
	$(INSTALL_DIR) $(libdir)
	$(INSTALL_PROGRAM) -s $(TARGETS-contrib-plugins) $(libdir)

clean::
	rm -f $(TARGETS-contrib-plugins)

libng/contrib-plugins/flt-smooth.so: libng/contrib-plugins/flt-smooth.o
libng/contrib-plugins/snd-alsa.so:   libng/contrib-plugins/snd-alsa.o
