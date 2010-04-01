
# variables
TARGETS-vbistuff := vbistuff/alevtd vbistuff/ntsc-cc

HTML-alevtd  := \
	vbistuff/alevt.css.h \
	vbistuff/top.html.h \
	vbistuff/bottom.html.h \
	vbistuff/about.html.h \

OBJS-alevtd  := \
	vbistuff/main.o \
	vbistuff/request.o \
	vbistuff/response.o \
	vbistuff/page.o \
	libng/devices.o \
	libvbi/libvbi.a
OBJS-ntsc-cc := \
	vbistuff/ntsc-cc.o

# local targets
vbistuff/alevtd: $(OBJS-alevtd)
	$(CC) $(CFLAGS) -o $@ $(OBJS-alevtd)

vbistuff/ntsc-cc: $(OBJS-ntsc-cc)
	$(CC) $(CFLAGS) -o $@  $(OBJS-ntsc-cc) $(ATHENA_LIBS)


# global targets
all:: $(TARGETS-vbistuff)

install::
	$(INSTALL_PROGRAM) -s $(TARGETS-vbistuff) $(bindir)

clean::
	rm -f $(HTML-alevtd)

distclean::
	rm -f $(TARGETS-vbistuff)

# special dependences
vbistuff/main.o:: vbistuff/main.c $(HTML-alevtd)
