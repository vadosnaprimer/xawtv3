
# passed to configure
prefix = /usr/local

# arch name -- for build directory
arch := $(shell echo "arch-`uname -m`-`uname -s`" | tr "A-Z" "a-z")

# targets
build all install: $(arch)/Makefile
	$(MAKE) -C $(arch) $@

clean distclean:
	-test -d "$(arch)" && rm -rf "$(arch)"

tarball rpm dsc debs pbuild release snapshot snap:
	./configure
	$(MAKE) $@

$(arch)/Makefile:
	mkdir -p $(arch)
	(cd $(arch); ../configure	\
		--prefix=$(prefix)	)
