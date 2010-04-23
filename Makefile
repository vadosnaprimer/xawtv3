
# passed to configure
prefix = /usr/local

# arch name -- for build directory
arch := $(shell echo "arch-`uname -m`-`uname -s`" | tr "A-Z" "a-z")

# targets
build all install: $(arch)/Makefile configure
	$(MAKE) -C $(arch) $@

clean distclean:
	-test -d "$(arch)" && rm -rf "$(arch)"

configure:
	autoconf

tarball rpm dsc debs pbuild release snapshot snap: configure
	./configure
	$(MAKE) $@

$(arch)/Makefile: configure
	mkdir -p $(arch)
	(cd $(arch); ../configure	\
		--prefix=$(prefix)	)
