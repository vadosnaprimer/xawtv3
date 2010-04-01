# Generated automatically from Makefile.in by configure.
CC=gcc
CFLAGS=-O2 -Wall -I..

PROGS=webcam

##########################################################################

all: $(PROGS)

webcam: webcam.o
	$(CC) $(CFLAGS) -o $@ $^ -ljpeg	# -ljpeg

clean:
	-rm -f *.o *.moc *~ core* *.bak TAGS

realclean distclean: clean
	-rm -f $(PROGS) Makefile

dep depend install:

