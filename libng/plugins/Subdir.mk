
# targets to build
TARGETS-plugins := \
	libng/plugins/flt-invert.so \
	libng/plugins/flt-gamma.so \
	libng/plugins/conv-mjpeg.so \
	libng/plugins/write-avi.so
ifeq ($(FOUND_LQT),yes)
TARGETS-plugins += \
	libng/plugins/write-qt.so
endif
ifeq ($(FOUND_OS),linux)
TARGETS-plugins += \
	libng/plugins/drv0-v4l2.so \
	libng/plugins/drv1-v4l.so \
	libng/plugins/snd-oss.so
endif
ifeq ($(FOUND_OS),bsd)
TARGETS-plugins += \
	libng/plugins/drv0-bsd.so \
	libng/plugins/snd-oss.so
endif

GONE-plugins := \
	$(libdir)/invert.so \
	$(libdir)/nop.so \
	$(libdir)/flt-nop.so

# libraries to link
libng/plugins/write-qt.so : LDLIBS := $(QT_LIBS)

# global targets
all:: $(TARGETS-plugins)

install::
	$(INSTALL_DIR) $(libdir)
	$(INSTALL_PROGRAM) -s $(TARGETS-plugins) $(libdir)
	rm -f $(GONE-plugins)

clean::
	rm -f $(TARGETS-plugins)
