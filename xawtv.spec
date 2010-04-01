%define		prefix	/usr/X11R6

# workaround a bug in rpm 3.0.4:
#   by default it modifies the files installed by my %install
#   scriptlet (compresses the man-pages), but failes to update
#   the %files section too.
%define __os_install_post       true

Summary: Video4Linux Stream Capture Viewer
Name: xawtv
Version: 3.37
Release: 1
Source0: xawtv_%{version}.tar.gz
Group: X11/Applications
Copyright: GNU GENERAL PUBLIC LICENSE
URL: http://www.strusel007.de/linux/xawtv/xawtv_%{version}.tar.gz
BuildRoot: /var/tmp/xawtv-%{version}.root

%package radio
Summary: radio
Group: Applications/Sound

%package misc
Summary: misc
Group: X11/Applications

%package webcam
Summary: webcam
Group: Graphics

%package -n alevtd
Summary: alevtd
Group: Applications/Internet

%description
A collection tools for video4linux:
 * xawtv    - X11 TV application
 * fbtv     - console TV application
 * streamer - capture tool (images / movies)
 * v4lctl   - command line tool to control v4l devices

%description radio
This is a ncurses-based radio application

%description misc
This package has a few tools you might find useful.  They
have not to do very much to do with xawtv.  I've used/wrote
them for debugging:
 * propwatch   - monitors properties of X11 windows.  If you
                 want to know how to keep track of xawtv's
                 _XAWTV_STATION property, look at this.
 * dump-mixers - dump mixer settings to stdout
 * record      - console sound recorder.  Has a simple input
                 level meter which might be useful to trouble
                 shoot sound problems.
 * showriff    - display the structure of RIFF files (avi, wav).

%description webcam
webcam captures images from a video4linux device like bttv,
annotates them and and uploads them to a webserver using ftp
in a endless loop.

%description -n alevtd
http daemon which serves videotext pages as HTML.

%prep
%setup

%build
mkdir build
cd build
CFLAGS="$RPM_OPT_FLAGS" ../configure --prefix=%{prefix}
make

%install
cd build
make ROOT="$RPM_BUILD_ROOT" SUID_ROOT="" install
find "$RPM_BUILD_ROOT" -type f -print		\
	| sed -e "s|$RPM_BUILD_ROOT||"		\
	| grep -v "^/usr/doc"			\
	> rpm.all

egrep -e "/radio"					rpm.all	> rpm.radio
egrep -e "/(dump-mixers|record|showriff|propwatch)"	rpm.all	> rpm.misc
egrep -e "/webcam"					rpm.all	> rpm.webcam
egrep -e "/alevtd"					rpm.all	> rpm.alevtd
egrep -ve "/(radio|dump-mixers|record|showriff|propwatch|webcam|alevtd|v4l-conf)"	\
	rpm.all > rpm.xawtv

%files -f build/rpm.xawtv
%defattr(-,root,root)
%attr(4711,root,root) %{prefix}/bin/v4l-conf
%doc README Changes COPYING Programming-FAQ Trouble-Shooting Sound-FAQ
%doc README.* UPDATE_TO_v3.0
%doc contrib/dot.lircrc contrib/frequencies*

%files radio -f build/rpm.radio
%defattr(-,root,root)

%files misc -f build/rpm.misc
%defattr(-,root,root)
%doc tools/README

%files webcam -f build/rpm.webcam
%defattr(-,root,root)
%doc webcam/webcam.cgi

%files -n alevtd -f build/rpm.alevtd
%defattr(-,root,root)

%clean
rm -rf $RPM_BUILD_ROOT

%post
cd /usr/X11R6/lib/X11/fonts/misc
mkfontdir
xset fp rehash || true
