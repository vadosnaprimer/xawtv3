
srcdir=.


include ../Make.config

CFLAGS=-g -g -O2 $(WARN_FLAGS) $(X11_FLAGS) -I..
LDLIBS=$(ATHENA_LIBS)

all build: ntsc-cc

ntsc-cc: ntsc-cc.o
	$(CC) $(CFLAGS) -o $@ ntsc-cc.o $(LDLIBS)

install: all
	$(INSTALL_DIR) $(bindir)
	$(INSTALL_DIR) $(mandir)
	$(INSTALL_PROGRAM) -s ntsc-cc $(bindir)
	$(INSTALL_DATA) $(srcdir)/ntsc-cc.man $(mandir)/man1/ntsc-cc.1

clean distclean realclean:
	-rm -f ntsc-cc ntsc-cc.o *~

depend dep:

