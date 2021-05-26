srcdir		:= .
VPATH		:= $(srcdir)

# for package builds (buildroot install + no root privs needed)
DESTDIR=
SUID_ROOT=-m4755 -o root
STRIP_FLAG=

# install paths
prefix		:= /usr/local
exec_prefix	:= ${prefix}
bindir		:= $(DESTDIR)${exec_prefix}/bin
mandir		:= $(DESTDIR)${prefix}/share/man
libdir		:= $(DESTDIR)${exec_prefix}/lib/xawtv
datadir		:= $(DESTDIR)${prefix}/share/xawtv
resdir		:= $(DESTDIR)/usr/share/X11
config		:= /etc/X11/xawtvrc

# programs
CC		:= gcc
CXX		:= g++
INSTALL		:= /usr/bin/install -c
INSTALL_PROGRAM := ${INSTALL} $(STRIP_FLAG)
INSTALL_DATA	:= ${INSTALL} -m 644
INSTALL_DIR	:= /usr/bin/install -c -d -m 755

# misc
VERSION		:= 3.107

# for CFLAGS
WARN_FLAGS	:= -Wall -Wmissing-prototypes -Wstrict-prototypes -Wpointer-arith -Wno-pointer-sign
LFS_FLAGS	:= -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64
X11_FLAGS	:=  -I/usr/include/freetype2 -I/usr/include/libpng16  -I/usr/include/X11/fonts
LIB_FLAGS	:= -I. -I./vbistuff -I./x11 \
		   -I$(srcdir)/jwz -I$(srcdir)/common -I$(srcdir)/console \
		   -I$(srcdir)/x11 -I$(srcdir)/structs \
		   -I$(srcdir)/libng
LD_FLAGS	:= -Llibng

# various libraries
ATHENA_LIBS	:=  -lXft -lfontconfig -lfreetype  -lXv -lXrandr -lXrender -lXinerama -lXxf86vm -lXxf86dga  -lXaw -lXmu -lXt  -lSM -lICE -lXpm -lXext -lX11 
MOTIF_LIBS	:=  -lXft -lfontconfig -lfreetype  -lXv -lXrandr -lXrender -lXinerama -lXxf86vm -lXxf86dga  -lXm -lXmu -lXt  -lSM -lICE \
		   -lXpm -lXext -lX11 
THREAD_LIBS	:= -lpthread
CURSES_LIBS	:= -lncursesw
LIRC_LIBS	:= -llirc_client
OSS_LIBS	:= 
ALSA_LIBS	:= -lasound
AA_LIBS		:= -laa
QT_LIBS		:= -lquicktime -ldl -Wl,-E -lglib-2.0  -lm
QT_FLAGS	:= -I/usr/include/lqt 
VBI_LIBS	:= -lzvbi -lpthread -lm -lpng -lz
GL_LIBS		:= -lGL -lm
DV_LIBS		:= -ldv -lm
DLFLAGS		:= -ldl -Wl,-E

# stuff configure has found
FOUND_AALIB	:= yes
FOUND_ALSA	:= yes
FOUND_DV	:= yes
FOUND_GL	:= yes
FOUND_LQT	:= yes
FOUND_MOTIF	:= yes
FOUND_OS	:= linux
FOUND_X11	:= yes
FOUND_ZVBI	:= yes
USE_MMX		:= no
LIBV4L		:= yes
FOUND_EXPLAIN	:= yes

# build final cflags
CFLAGS   := -g -O2 -I/usr/include/ncursesw
CFLAGS   += $(WARN_FLAGS)
CFLAGS   += $(LFS_FLAGS)
CFLAGS   += $(X11_FLAGS)
CFLAGS   += $(LIB_FLAGS)
CFLAGS   += $(QT_FLAGS)
CFLAGS   += -DCONFIGFILE='"$(config)"'
CFLAGS   += -DLIBDIR='"$(libdir)"'
CFLAGS   += -DDATADIR='"$(datadir)"'
CFLAGS   += -DVERSION='"$(VERSION)"'
CXXFLAGS := $(CFLAGS)

# for gcc3
#CFLAGS   += -std-gnu99

# shared objects need -fPIC
%.so : CFLAGS   += -fPIC
%.so : CXXFLAGS += -fPIC

# libraries
LDLIBS  := 


#########################################################
# targets

build: all

Makefile: $(srcdir)/Makefile.in $(srcdir)/configure
	$(srcdir)/configure

$(srcdir)/configure: $(srcdir)/configure.ac
	(cd $(srcdir); autoconf && autoheader && rm -rf autom4te.cache)

install:: all
	$(INSTALL_DIR) $(bindir)

clean::
	find . -name \*~ -print | xargs rm -f
	find . -name \*.o -print | xargs rm -f
	find . -name \*.a -print | xargs rm -f
	find . -name \*.dep -print | xargs rm -f
	rm -f $(depfiles)

distclean:: clean
	-rm -f Makefile Make.config
	-rm -f config.cache config.h config.log config.status
	cp Makefile.clean Makefile

realclean:: distclean
	find . -name snap0*.ppm  -print | xargs -i rm -f
	find . -name snap0*.jpeg -print | xargs -i rm -f
	find . -name .nfs* -print | xargs -i rm -f
	find . -name core.* -print | xargs -i rm -f


#########################################################
# some rules ...

include $(srcdir)/mk/Compile.mk

%.h: %.in
	perl $(srcdir)/scripts/html.pl < $< > $@

%.h: %.ad
	perl $(srcdir)/scripts/fallback.pl < $< > $@


#########################################################
# include stuff

# must come first
include $(srcdir)/common/Subdir.mk

# subdirs
include $(srcdir)/console/Subdir.mk
include $(srcdir)/debug/Subdir.mk
include $(srcdir)/frequencies/Subdir.mk
include $(srcdir)/libng/Subdir.mk
include $(srcdir)/libng/plugins/Subdir.mk
include $(srcdir)/libng/contrib-plugins/Subdir.mk
include $(srcdir)/man/Subdir.mk
include $(srcdir)/scripts/Subdir.mk
include $(srcdir)/vbistuff/Subdir.mk
include $(srcdir)/x11/Subdir.mk

# dependencies
-include $(depfiles)


#########################################################
# just for me, some maintaining jobs.  Don't use them

tag:
	@git tag -a -m -s "Tag as xawtv-3.107" xawtv-3.107
	@echo "Tagged as xawtv-3.107"

archive:
	@(cd $(srcdir) && git archive --format=tar --prefix=xawtv-3.107/ xawtv-3.107) > xawtv-3.107.tar
	# Ensure these are newer then configure.in
	@touch $(srcdir)/configure $(srcdir)/config.h.in
	@tar --transform='s#config#xawtv-3.107/config#' -rf xawtv-3.107.tar $(srcdir)/configure $(srcdir)/config.h.in
	@bzip2 -f xawtv-3.107.tar

