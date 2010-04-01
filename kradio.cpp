#include <stdio.h>
#include <iostream.h>
#include <stdlib.h> 
#include <unistd.h> 
#include <fcntl.h>
#include <errno.h>
#include <getopt.h>
#include <sys/ioctl.h>

#include <qkeycode.h>
#include <qlabel.h>
#include <qaccel.h>
#include <qpalette.h>
#include <qtooltip.h>

#include <kiconloader.h>
#include <klocale.h>

#include "kradio.moc"

#define _(TEXT) klocale->translate(TEXT)

/* ------------------------------------------------------------------------ */

KApplication *globalKapp;
KIconLoader  *globalKIL;
KLocale      *globalKlocale;

char *device = "/dev/radio";

int main(int argc, char **argv)
{
    globalKapp     = new KApplication( argc, argv, "kradio");
    globalKIL      = globalKapp->getIconLoader();
    globalKlocale  = globalKapp->getLocale();
    KRadio *kradio;
    int c;

    /* parse options */
    for (;;) {
	if (-1 == (c = getopt(argc, argv, "hc:")))
	    break;
	switch (c) {
	case 'c':
	    device = optarg;
	    break;
	case 'h':
	default:
	    fprintf(stderr,
		    "usage: %s  [ options ] \n"
		    "\n"
		    "options:\n"
		    "    -c <dev>  radio device    [%s]\n",
		    argv[0],
		    device);
	    exit(1);
	}
    }

    kradio = new KRadio();
    return globalKapp->exec();
}

/* ------------------------------------------------------------------------ */

#include <asm/types.h>          /* XXX glibc */

#include "config.h"
#if USE_KERNEL_VIDEODEV
# include <linux/videodev.h>
#else
# include "videodev.h"
#endif

static int hw_fd = -1;

static struct video_audio hw_audio;

static int hw_init()
{
    if (-1 == (hw_fd = open(device,O_RDONLY))) {
	fprintf(stderr,"open %s: %s\n",device,strerror(errno));
	exit(1);
    }
    return hw_fd;
}

static int hw_tune(int freq)
{
    int ifreq = ((freq/1000)*16)/1000;

    if (-1 == hw_fd)
	return -1;

    return ioctl(hw_fd, VIDIOCSFREQ, &ifreq);
}

static int hw_getaudio()
{
    return ioctl(hw_fd, VIDIOCGAUDIO, &hw_audio);
}

static int hw_setaudio()
{
    return ioctl(hw_fd, VIDIOCSAUDIO, &hw_audio);
}

/* ------------------------------------------------------------------------ */

KRadio::KRadio() : KTopLevelWidget("main")
{
    globalKapp->setMainWidget(this);
    config  = KApplication::getKApplication()->getConfig();
    iloader = KApplication::getKApplication()->getIconLoader();
    iloader->insertDirectory(0,"."); /* handy for testing without install */
    mute_on  = iloader->loadIcon("speaker_on.xpm");
    mute_off = iloader->loadIcon("speaker_off.xpm");

    freq = mem = 0;
    memset(fr,0,sizeof(int)*STATION_BUTTONS);

    hw_init();
    hw_getaudio();
    hw_audio.flags |= VIDEO_AUDIO_MUTE;
    hw_setaudio();
    
    led  = mkled();
    mkpanel();
    setView(cont);

    /* session management */
    if (globalKapp->isRestored() && canBeRestored(0)) {
	restore(0);
    } else {
	show();
    }

    freq = 87500000;
    tune(fr[0]);
    mute();
}

KRadio::~KRadio()
{
}

void KRadio::mkpanel()
{
    QString  fs,ns;
    int      x,i,y1,y2,y3;
    char     label[16];
    cont = new QWidget(this);

    station = new QLabel(freq_to_str(freq), cont, "station");
    station->setFont(QFont("ledfixed"));
    station->setPalette(led);
    station->setMargin(5);
    station->setAlignment(AlignCenter);
    y1 = station->sizeHint().height();

    edit = new QLineEdit(cont,"edit");
    edit->setFont(QFont("ledfixed"));
    edit->setPalette(led);
    edit->hide();

    btg = new QButtonGroup(cont,"btg");
    for (i = 0; i < STATION_BUTTONS; i++) {
	sprintf(label,"%d",i+1);
	config->setGroup("Buttons");
	fr[i] = config->readNumEntry(label);
	fs = freq_to_num(fr[i]);
	ns = freq_to_str(fr[i]);
	bt[i] = new QPushButton(btg,label);
	bt[i]->setAccel(Key_F1 + i);
	bt[i]->setText(fs);
	QToolTip::add(bt[i],ns);
    }
    y2 = bt[0]->sizeHint().height();

    addbutton(0, _("quit"), _("quit kradio"), "exit.xpm",
	      SLOT(quit()), CTRL+Key_Q );
    addbutton(1, _("help"), _("display a short description"), "help.xpm",
	      SLOT(help()), 0);
    addbutton(2, _("edit"), _("edit station name"), NULL,
	      SLOT(startedit()), CTRL+Key_E);
    addbutton(3, _("set"), _("program the station buttons:\n"
			     "press first this one, then the station button"), NULL,
	      SLOT(memory()), 0);
    addbutton(4, _("up"), _("tune up"), NULL,
	      SLOT(tuneup()), Key_Up);
    addbutton(5, _("down"), _("tune down"), NULL,
	      SLOT(tunedown()), Key_Down);
    addbutton(6, _("scan"), _("not implemented yet\n"
			      "will scan for the next station later"), NULL,
	      NULL, CTRL+Key_S);
    addbutton(7, _("mute"), _("mute"), NULL,
	      SLOT(mute()), CTRL+Key_M);
    y3 = ct[0]->sizeHint().height();

    station->setGeometry(0,0,PANEL_WIDTH,y1);
    edit->setGeometry(0,0,PANEL_WIDTH,y1);

    btg->setGeometry(0,y1,PANEL_WIDTH,y2);
    for (i = 0; i < STATION_BUTTONS; i++) {
	bt[i]->setGeometry(i*PANEL_WIDTH/STATION_BUTTONS,0,
			   PANEL_WIDTH/STATION_BUTTONS,y2);
    }
    for (i = 0; i < CONTROL_BUTTONS; i++) {
	ct[i]->setGeometry(i*PANEL_WIDTH/CONTROL_BUTTONS,y1+y2,
			   PANEL_WIDTH/CONTROL_BUTTONS,y3);
    }
    x = PANEL_WIDTH;

#if 0 /* does'nt work ... */
    if (hw_audio.flags & VIDEO_AUDIO_VOLUME) {
	volume = new QSlider(0,65535,512,65535-hw_audio.volume,
			     QSlider::Vertical,this,"volume");
	QToolTip::add(volume,_("Volume"));
	connect(volume,SIGNAL(valueChanged(int)), this, SLOT(vol(int)));

	i = volume->sizeHint().width();
	volume->setGeometry(x+2,0,i,y1+y2+y3);
	x += i+4;
    }
#endif

    this->resize(x,y1+y2+y3);
    this->setMaximumWidth(x);
    this->setMinimumWidth(x);
    this->setMaximumHeight(y1+y2+y3);
    this->setMinimumHeight(y1+y2+y3);

    connect(edit,SIGNAL(returnPressed()), this, SLOT(stopedit()));
    connect(btg,SIGNAL(clicked(int)), this, SLOT(stationbutton(int)));
}

void KRadio::addbutton(int i, const char *name, const char *tool,
		       const char *icon, const char *slot, int accel)
{
    QPixmap pix;
    
    if (icon) {
	pix = iloader->loadIcon(icon);
	ct[i] = new QPushButton(cont,name);
	ct[i]->setPixmap(pix);
    } else {
	ct[i] = new QPushButton(name,cont,name);
    }
    if (accel)
	ct[i]->setAccel(accel);
    if (tool)
	QToolTip::add(ct[i],tool);
    if (slot)
	connect(ct[i],SIGNAL(clicked()), this, slot);
}

QPalette KRadio::mkled()
{
    QColor      fore("lightgreen"),back("black"),editback("#505050");
    QColorGroup grp(fore,back, back,back,back, fore, editback);
    QPalette    pal(grp,grp,grp);

    return pal;
}

QString KRadio::freq_to_name(int freq)
{
    QString name;
    char line[64];

    config->setGroup("Stations");
    sprintf(line,"%d",freq);
    name = config->readEntry(line);
    if (0 == strlen(name))
	return NULL;
    return name;
}

QString KRadio::freq_to_num(int freq)
{
    char line[64];

    if (0 == freq) {
	strcpy(line,_("-"));
    } else if (freq > 10000000 /* 10 MHz */) {
	sprintf(line,"%d.%d",freq/1000000,(freq/100000)%10);
    } else {
	strcpy(line,"oops");
    }
    return(QString(line));
}

QString KRadio::freq_to_str(int freq)
{
    QString name;

    if (NULL != (name = freq_to_name(freq)))
	return name;
    name = freq_to_num(freq);
    if (freq > 10000000 /* 10 MHz */) {
	name = name + " MHz";
    }
    
    return name;
}

void KRadio::quit()
{
    exit(0);
}

/* ------------------------------------------------------------------------ */

void KRadio::startedit()
{
    char line[64];

    if (edit->isVisible())
	return;

    config->setGroup("Stations");
    sprintf(line,"%d",freq);
    edit->setText(config->readEntry(line));
    edit->show();
    edit->setFocus();
}

void KRadio::stopedit()
{
    char line[64];

    if (!edit->isVisible())
	return;

    config->setGroup("Stations");
    sprintf(line,"%d",freq);
    if (0 == strlen(edit->text())) {
	/* XXX delete key ??? */
	config->writeEntry(line,"",true,false);
    } else {
	config->writeEntry(line,edit->text(),true,false);
    }
    config->sync();
    station->setText(freq_to_str(freq));
    edit->hide();
}

/* ------------------------------------------------------------------------ */

void KRadio::stationbutton(int nr)
{
    QString  fs,ns;
    char line[64];

    if (mem) {
	fr[nr] = freq;
	QToolTip::remove(bt[nr]);

	fs = freq_to_num(fr[nr]);
	ns = freq_to_str(fr[nr]);
	bt[nr]->setText(fs);
	QToolTip::add(bt[nr],ns);

	memory();
	sprintf(line,"%d",nr+1);
	config->setGroup("Buttons");
	config->writeEntry(line,freq,true,false);
	config->sync();
    } else {
	tune(fr[nr]);
    }
}

void KRadio::memory()
{
    mem = !mem;
    ct[3]->setText(mem ? _("set *") : _("set"));
}

void KRadio::help()
{
    QMessageBox::about
	(NULL, _("Help"),
	 "This is kradio, a KDE application to control the WinTV/Radio card.\n"
	 "(c) 1998 Gerd Knorr <kraxel@cs.tu-berlin.de>\n"
	 "\n"
	 "Usage is quite simple: There is the station display.  The upper\n"
	 "button row are the station buttons.  The lower row are the\n"
	 "control functions. This works just like a normal Radio does.\n"
	 "\n"
	 "There are Hotkeys:  The function keys for the station buttons,\n"
	 "the cursor keys (up/down) for tuning, Ctrl+Q for quit, Ctrl+E \n"
	 "for edit (the station name), Ctrl+S for scan and Ctrl-M for mute\n"
	 "\n"
	 "Have fun,\n\n     Gerd");
}

void KRadio::tune(int freq)
{
    if (0 == freq)
	return;
    this->freq = freq;
    hw_tune(freq);
    station->setText(freq_to_str(freq));
}

void KRadio::tuneup()
{
    freq += 100000;
    tune(freq);
}

void KRadio::tunedown()
{
    freq -= 100000;
    tune(freq);
}

/* ------------------------------------------------------------------------ */

void KRadio::mute()
{
    hw_audio.flags ^= VIDEO_AUDIO_MUTE;
    hw_setaudio();
    ct[7]->setPixmap((hw_audio.flags & VIDEO_AUDIO_MUTE) ? mute_off : mute_on);
}

void KRadio::vol(int val)
{
    hw_audio.volume = 65535-val;
    fprintf(stderr,"vol: %d\n",hw_audio.volume);
    hw_setaudio();
}
