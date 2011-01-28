#!/bin/sh
inst=$(echo /usr/share/automake*/install-sh | head -1)
set -ex
autoconf
autoheader
rm -rf autom4te.cache
cp "$inst" .
