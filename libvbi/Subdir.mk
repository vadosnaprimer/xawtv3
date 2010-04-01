# from alevt
OBJS-libvbi :=	\
	libvbi/vbi.o \
	libvbi/fdset.o \
	libvbi/misc.o \
	libvbi/hamm.o \
	libvbi/lang.o \
	libvbi/cache.o \
	libvbi/font.o \
	libvbi/export.o \
	libvbi/exp-gfx.o \
	libvbi/exp-html.o \
	libvbi/exp-txt.o

libvbi/libvbi.a: $(OBJS-libvbi)
	rm -f $@
	ar -r $@ $(OBJS-libvbi)
	ranlib $@
