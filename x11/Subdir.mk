
# variables
TARGETS-x11 :=

ifeq ($(FOUND_X11),yes)
TARGETS-x11 += \
	x11/propwatch \
	x11/v4lctl \
	x11/xawtv-remote \
	x11/rootv \
	x11/xawtv
endif
ifeq ($(FOUND_MOTIF),yes)
TARGETS-x11 += \
	x11/motv
endif

OBJS-xawtv := \
	x11/wmhooks.o \
	x11/xawtv.o \
	x11/x11.o \
	x11/xt.o \
	x11/xv.o \
	x11/toolbox.o \
	x11/conf.o \
	x11/complete-xaw.o \
	jwz/remote.o \
	common/channel.o \
	$(OBJS-common-input) \
	$(OBJS-common-capture) \
	libvbi/libvbi.a
LIBS-xawtv := \
	$(THREAD_LIBS) $(CURSES_LIBS) $(LIRC_LIBS) $(ALSA_LIBS) \
	$(ATHENA_LIBS) -ljpeg -lm

OBJS-motv := \
	x11/motv.o \
	x11/man.o \
	x11/icons.o \
	x11/wmhooks.o \
	x11/x11.o \
	x11/xt.o \
	x11/xv.o \
	x11/complete-motif.o \
	jwz/remote.o \
	common/RegEdit.o \
	common/channel-no-x11.o \
	$(OBJS-common-input) \
	$(OBJS-common-capture) \
	libvbi/libvbi.a
LIBS-motv := \
	$(THREAD_LIBS) $(CURSES_LIBS) $(LIRC_LIBS) $(ALSA_LIBS) \
	$(MOTIF_LIBS) -ljpeg -lm

OBJS-v4lctl := \
	x11/v4lctl.o \
	x11/xv.o \
	common/channel-no-x11.o \
	$(OBJS-common-capture)
LIBS-v4lctl := \
	$(THREAD_LIBS) $(ATHENA_LIBS) -ljpeg -lm

OBJS-rootv := \
	x11/rootv.o \
	common/parseconfig.o
LIBS-rootv := \
	$(ATHENA_LIBS)

LANGUAGES := de it
MOTV-app  := $(patsubst %,x11/MoTV.%.ad,$(LANGUAGES))


# local targets
x11/xawtv-remote: x11/xawtv-remote.o
	$(CC) $(CFLAGS) -o $@  $< $(ATHENA_LIBS)

x11/propwatch: x11/propwatch.o
	$(CC) $(CFLAGS) -o $@  $< $(ATHENA_LIBS)

x11/rootv: $(OBJS-rootv)
	$(CC) $(CFLAGS) -o $@  $(OBJS-rootv) $(LIBS-rootv) $(DLFLAGS)

x11/v4lctl: $(OBJS-v4lctl)
	$(CC) $(CFLAGS) -o $@  $(OBJS-v4lctl) $(LIBS-v4lctl) $(DLFLAGS)

x11/xawtv: $(OBJS-xawtv)
	$(CC) $(CFLAGS) -o $@  $(OBJS-xawtv) $(LIBS-xawtv) $(DLFLAGS)

x11/motv: $(OBJS-motv)
	$(CC) $(CFLAGS) -o $@  $(OBJS-motv) $(LIBS-motv) $(DLFLAGS)

x11/complete-xaw.o:: x11/complete.c
	$(CC) $(CFLAGS) -DATHENA=1 -Wp,-MD,$*.dep -c -o $@ $<
	@sed -e "s|.*\.o:|$@::|" < $*.dep > $*.d && rm -f $*.dep

x11/complete-motif.o:: x11/complete.c
	$(CC) $(CFLAGS) -DMOTIF=1 -Wp,-MD,$*.dep -c -o $@ $<
	@sed -e "s|.*\.o:|$@::|" < $*.dep > $*.d && rm -f $*.dep


# global targets
ifeq ($(FOUND_X11),yes)
all:: $(TARGETS-x11)
endif
ifeq ($(FOUND_MOTIF),yes)
all:: $(MOTV-app)
endif

ifeq ($(FOUND_X11),yes)
install::
	$(INSTALL_PROGRAM) -s $(TARGETS-x11) $(bindir)
	$(INSTALL_DIR) $(resdir)/app-defaults
	$(INSTALL_DATA) $(srcdir)/x11/Xawtv.ad $(resdir)/app-defaults/Xawtv
endif
ifeq ($(FOUND_MOTIF),yes)
install::
	$(INSTALL_DATA) x11/MoTV.ad $(resdir)/app-defaults/MoTV
	for lang in $(LANGUAGES); do \
	    $(INSTALL_DIR) $(resdir)/$$lang/app-defaults; \
	    $(INSTALL_DATA) x11/MoTV.$$lang.ad \
		$(resdir)/$$lang/app-defaults/MoTV; \
	done
endif

distclean::
	rm -f $(TARGETS-x11)
	rm -f $(MOTV-app) x11/MoTV.ad x11/MoTV.h x11/Xawtv.h

# special dependences / rules
x11/xawtv.o:: x11/xawtv.c x11/Xawtv.h
x11/motv.o:: x11/motv.c x11/MoTV.h

x11/MoTV.ad: $(srcdir)/x11/MoTV-default $(srcdir)/x11/MoTV-fixed
	cat $(srcdir)/x11/MoTV-default $(srcdir)/x11/MoTV-fixed > x11/MoTV.ad

x11/MoTV.%.ad: x11/MoTV-%
	cat $< $(srcdir)/x11/MoTV-fixed > $@
