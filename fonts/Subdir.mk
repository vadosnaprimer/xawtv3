FONTSERVER := unix/:7100
FONTSPEC   := -misc-fixed-medium-r-semicondensed-*-13-120-75-75-c-60

export FONTSERVER

PCF :=  fonts/led-iso8859-1.pcf.gz \
	fonts/led-iso8859-2.pcf.gz \
	fonts/led-iso8859-15.pcf.gz \
	fonts/led-koi8-r.pcf.gz
BDF := $(PCF:pcf.gz=bdf)

$(BDF):
	charset=`echo $@ | sed -e 's|.*led-||' -e 's|.bdf||'`;	\
	perl $(srcdir)/scripts/bigfont.pl -fn "$(FONTSPEC)-$$charset" > $@

all:: $(PCF)

install::
	$(INSTALL_DIR) $(fontdir)
	$(INSTALL_DATA) $(PCF) $(fontdir)

clean::
	rm -f fonts/*.pcf.gz

realclean::
	rm -f fonts/*.bdf

