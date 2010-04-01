# GNUmakefile for the videotext-driver
#
# Copyright (c) 1994-97 Martin Buck  <martin-2.buck@student.uni-ulm.de>
#
# $Id: Makefile,v 1.6 1998/03/29 12:19:50 richard Exp $

# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307,
# USA.


# Uncomment the suitable line for your VTX-interface

# IF_WINTVPCI   is a PCI card containing a video grabber, a tuner and a
#               videotext interface made by Hauppauge Computer Works, Inc.
#               (thanks to Ralph Metzler for patches)

VTXIF = IF_WINTVPCI


# Choose your favourite major-number here (can be overridden on the insmod-
# commandline with major=??):

VTX_DEFAULT_MAJOR = 28


# To get debugging messages, uncomment the following line and use debug=x
# (with x > 0) when loading the driver (WARNING: x >= 2 is *very* verbose, you
# definitely want to kill your klogd before you do this)

DEBUG = -DDEBUG_VTX


# Uncomment the next line to make the driver quiet on (un)loading (i.e. no
# version- and interface-type messages). This is useful if you load vtx with
# kerneld. You can override this setting with quiet=0 or quiet=1 on the
# insmod-commandline (or in /etc/conf.modules).

#VTX_QUIET = -DVTX_QUIET


#
# Module compile magic
#

CURRENT=$(shell uname -r)

KERNEL_LOCATION=/usr/src/linux

M_OBJS = vtx.o
EXTRA_CFLAGS = -DEXPORT_SYMTAB -DVTX_DEFAULT_MAJOR=$(VTX_DEFAULT_MAJOR) \
	$(DEBUG) $(VTX_QUIET)

here:
	DIR=`pwd`; (cd $(KERNEL_LOCATION); make SUBDIRS=$$DIR modules)

install:
	su -c "cp -v $(M_OBJS) /lib/modules/$(CURRENT)/misc"

clean:
	-rm -f $(M_OBJS) .*.o.flags *~

include $(KERNEL_LOCATION)/Rules.make

