FONTDIR=/usr/X11R6/lib/X11/fonts/misc
BDF=$(shell echo *.bdf)
PCF=$(subst .bdf,.pcf,$(BDF))

all: $(PCF)
	mkfontdir

install: all
	for file in $(PCF); do \
		install -m 644 $$file $(FONTDIR); \
	done
	(cd $(FONTDIR); mkfontdir)
	-xset fp rehash

clean:
	rm -f *.pcf fonts.dir

%.pcf: %.bdf
	bdftopcf -o $@ $<

###########################################################################
# don't use these, unless you know what you are doing...

SERVER=tcp/goldbach:7100

bdf:
	perl ./bigfont.pl -s $(SERVER) -fn "fixed" > led-fixed.bdf
#	perl ./bigfont.pl -s $(SERVER) -fn "6x10"  > led-6x10.bdf
#	perl ./bigfont.pl -s $(SERVER) -fn \
#		"-adobe-helvetica-medium-r-normal--10-*" > led-helv-10.bdf
#	perl ./bigfont.pl -s $(SERVER) -fn \
#		"-adobe-helvetica-medium-r-normal--12-*" > led-helv-12.bdf

realclean: clean
	rm -f *~ *.bdf

dist: realclean bdf

