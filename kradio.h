#ifndef KRADIO_H
#define KRADIO_H

#include <qdialog.h>
#include <qmsgbox.h>
#include <qpopmenu.h>
#include <qmenubar.h>
#include <qtooltip.h>
#include <qlayout.h>
#include <qpushbt.h>
#include <qchkbox.h>
#include <qbttngrp.h>
#include <qradiobt.h>
#include <qlistbox.h>
#include <qaccel.h>
#include <qslider.h>

#include <kapp.h>
#include <kmsgbox.h>
#include <kmenubar.h>
#include <ktopwidget.h>
#include <ktabctl.h>

#define PANEL_WIDTH 360
#define STATION_BUTTONS 8                 /* should'nt be more than 10 */
#define CONTROL_BUTTONS 8

/* ------------------------------------------------------------------------ */

class KRadio : public KTopLevelWidget
{
    Q_OBJECT;

private:
    QPalette  mkled();
    void      mkpanel();
    void      addbutton(int i, const char *name, const char *tool,
			const char *icon, const char *slot, int accel);

    void      tune(int freq);
    QString   freq_to_name(int freq);       /* (configured) name */
    QString   freq_to_num(int freq);        /* freq */
    QString   freq_to_str(int freq);        /* name if present, freq else */

    int           freq,mem;
    QPalette      led;
    KConfig      *config;
    KIconLoader  *iloader;
    
    QWidget      *cont;
    QLabel       *station;
    QLineEdit    *edit;

    QButtonGroup *btg;
    int           fr[STATION_BUTTONS];
    QPushButton  *bt[STATION_BUTTONS];
    QPushButton  *ct[CONTROL_BUTTONS];

    QSlider      *volume;
    QPixmap      mute_on,mute_off;

public slots:
    void mute();
    void help();
    void quit();
    void startedit();
    void stopedit();
    void stationbutton(int i);
    void memory();
    void tuneup();
    void tunedown();

    void vol(int);

public:
    KRadio();
    ~KRadio();

};

#endif
