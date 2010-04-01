
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

# local targets
libng/plugins/flt-invert.so: libng/plugins/flt-invert.pic
	$(CC) $(CFLAGS) -shared -Wl,-soname,$@ -o $@ $<

libng/plugins/conv-mjpeg.so: libng/plugins/conv-mjpeg.pic
	$(CC) $(CFLAGS) -shared -Wl,-soname,$@ -o $@ $<

libng/plugins/write-avi.so: libng/plugins/write-avi.pic
	$(CC) $(CFLAGS) -shared -Wl,-soname,$@ -o $@ $<

libng/plugins/flt-gamma.so: libng/plugins/flt-gamma.pic
	$(CC) $(CFLAGS) -shared -Wl,-soname,$@ -o $@ $< -lm

libng/plugins/write-qt.so: libng/plugins/write-qt.pic
	$(CC) $(CFLAGS) -shared -Wl,-soname,$@ -o $@ $< $(QT_LIBS)

libng/plugins/drv0-v4l2.so: libng/plugins/drv0-v4l2.pic
	$(CC) $(CFLAGS) -shared -Wl,-soname,$@ -o $@ $<

libng/plugins/drv1-v4l.so: libng/plugins/drv1-v4l.pic
	$(CC) $(CFLAGS) -shared -Wl,-soname,$@ -o $@ $<

libng/plugins/drv0-bsd.so: libng/plugins/drv0-bsd.pic
	$(CC) $(CFLAGS) -shared -Wl,-soname,$@ -o $@ $<

libng/plugins/snd-oss.so: libng/plugins/snd-oss.pic
	$(CC) $(CFLAGS) -shared -Wl,-soname,$@ -o $@ $<

# global targets
all:: $(TARGETS-plugins)

install::
	$(INSTALL_DIR) $(libdir)
	$(INSTALL_PROGRAM) -s $(TARGETS-plugins) $(libdir)
	rm -f $(GONE-plugins)

clean::
	rm -f $(TARGETS-plugins)

