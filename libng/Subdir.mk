OBJS-libng := \
	libng/grab-ng.o \
	libng/devices.o \
	libng/writefile.o \
	libng/color_common.o \
	libng/color_packed.o \
	libng/color_lut.o \
	libng/color_yuv2rgb.o \
	libng/convert.o

libng/libng.a: $(OBJS-libng)
	rm -f $@
	ar -r $@ $(OBJS-libng)
	ranlib $@

clean::
	rm -f libng.a
