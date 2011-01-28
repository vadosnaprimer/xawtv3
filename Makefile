XAWTV_VERSION = 3.97

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

tag:
	@git tag -a -m "Tag as xawtv-$(XAWTV_VERSION)" xawtv-$(XAWTV_VERSION)
	@echo "Tagged as xawtv-$(XAWTV_VERSION)"

archive:
	@git archive --format=tar --prefix=xawtv-$(XAWTV_VERSION)/ xawtv-$(XAWTV_VERSION) > xawtv-$(XAWTV_VERSION).tar
	@bzip2 -f xawtv-$(XAWTV_VERSION).tar
