Name:         xawtv
Group:        Applications/Multimedia
Requires:     v4l-conf, tv-common
Autoreqprov:  on
Version:      3.77
Release:      0
License:      GPL
Summary:      Video4Linux TV application (Athena)
Source:       http://bytesex.org/xawtv/%{name}_%{version}.tar.gz
Buildroot:    /var/tmp/root.%{name}-%{version}

%description
xawtv is a X11 application for watching TV with your linux box.  It
supports video4linux devices (for example bttv cards, various USB
webcams, ...).  It uses the Athena widgets. 

%package      -n motv
Summary:      Video4Linux TV application (Motif)
Group:        Applications/Multimedia
Requires:     v4l-conf, tv-common
Provides:     xawtv:/usr/X11R6/bin/motv

%description  -n motv
motv is a X11 application for watching TV with your Linux box.  It
supports video4linux devices (for example bttv cards, various USB
webcams, ...).  It's based on xawtv's code, but uses Motif to provide a
better GUI.

%package      -n tv-common
Summary:      tools and some README's for motv and xawtv
Group:        Applications/Multimedia

%description  -n tv-common
This package includes some X11 fonts used by motv and xawtv, some
utilities for them (xawtv-remote for example), and some README files.

%package      -n v4l-conf
Summary:      video4linux configuration tool
Group:        Applications/Multimedia
Provides:     xawtv:/usr/X11R6/bin/v4l-conf

%description  -n v4l-conf
This is a small utility used to configure video4linux device drivers
(bttv for example).  xawtv, motv and fbtv need it.

%package      -n v4l-tools
Summary:      video4linux terminal / command line utilities.
Group:        Applications/Multimedia
Requires:     v4l-conf, tv-common
Provides:     xawtv:/usr/X11R6/bin/v4lctl

%description  -n v4l-tools
This package includes a bunch of command line utilities:  v4lctl to
control video4linux devices; streamer to record movies; fbtv to
watch TV on the framebuffer console; ttv to watch tv on any ttv (powered
by aalib), webcam for capturing and uploading images, a curses radio
application, ...

%package      -n alevtd
Summary:      http server for teletext pages
Group:        Networking/Daemons

%description  -n alevtd
alevtd reads the teletext pages from /dev/vbi and allows to fetch them
via http, i.e. you can read the teletext pages with a web browser.

%prep
%setup -q

%build
mkdir build
cd build
CFLAGS="$RPM_OPT_FLAGS" ../configure --prefix=/usr/X11R6
make

%install
(cd build; make DESTDIR="%{buildroot}" SUID_ROOT="" install)
gzip -v %{buildroot}/usr/X11R6/man/man*/*
find %{buildroot} -name Xawtv -print	|\
	sed -e 's|%{buildroot}||' > appdefaults.xawtv
find %{buildroot} -name MoTV -print	|\
	sed -e 's|%{buildroot}||' > appdefaults.motv

%files -f appdefaults.xawtv
%defattr(-,root,root)
%doc COPYING
%doc Changes TODO README README.*
%doc contrib/frequencies*
/usr/X11R6/bin/xawtv
/usr/X11R6/man/man1/xawtv.1.gz
/usr/X11R6/bin/rootv
/usr/X11R6/man/man1/rootv.1.gz

%files -n motv -f appdefaults.motv
%defattr(-,root,root)
%doc COPYING
%doc Changes TODO README README.*
/usr/X11R6/bin/motv
/usr/X11R6/man/man1/motv.1.gz

%files -n tv-common
%defattr(-,root,root)
%doc COPYING
/usr/X11R6/bin/subtitles
/usr/X11R6/man/man1/subtitles.1.gz
/usr/X11R6/bin/xawtv-remote
/usr/X11R6/man/man1/xawtv-remote.1.gz
/usr/X11R6/bin/propwatch
/usr/X11R6/man/man1/propwatch.1.gz
/usr/X11R6/man/man5/xawtvrc.5.gz
/usr/X11R6/lib/xawtv/*.so

%files -n v4l-conf
%defattr(-,root,root)
%doc COPYING
%attr(4711,root,root) /usr/X11R6/bin/v4l-conf
/usr/X11R6/man/man8/v4l-conf.8.gz

%files -n v4l-tools
%defattr(-,root,root)
%doc COPYING
/usr/X11R6/bin/radio
/usr/X11R6/man/man1/radio.1.gz
/usr/X11R6/bin/fbtv
/usr/X11R6/man/man1/fbtv.1.gz
/usr/X11R6/bin/ttv
/usr/X11R6/man/man1/ttv.1.gz
/usr/X11R6/bin/streamer
/usr/X11R6/man/man1/streamer.1.gz
/usr/X11R6/bin/v4lctl
/usr/X11R6/man/man1/v4lctl.1.gz
/usr/X11R6/bin/record
/usr/X11R6/man/man1/record.1.gz
/usr/X11R6/bin/dump-mixers
/usr/X11R6/man/man1/dump-mixers.1.gz
/usr/X11R6/bin/showriff
/usr/X11R6/man/man1/showriff.1.gz
/usr/X11R6/bin/scantv
/usr/X11R6/man/man1/scantv.1.gz
/usr/X11R6/bin/webcam
/usr/X11R6/man/man1/webcam.1.gz

%files -n alevtd
%defattr(-,root,root)
%doc COPYING
/usr/X11R6/bin/alevtd
/usr/X11R6/man/man1/alevtd.1.gz

%clean
if test "%{buildroot}" != ""; then
	rm -rf "%{buildroot}"
fi
