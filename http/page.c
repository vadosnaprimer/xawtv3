#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "httpd.h"

/* libvbi */
#include "vt.h"
#include "misc.h"
#include "fdset.h"
#include "vbi.h"
#include "lang.h"
#include "dllist.h"
#include "export.h"

void fmt_page(struct export *e, struct fmt_page *pg, struct vt_page *vtp);

#define BUFSIZE 4096

/* ---------------------------------------------------------------------- */

static char stylesheet[] =
#include "alevt.css.h"
;

static char page_about[] =
#include "about.html.h"
;

static char page_top[] =
#include "top.html.h"
;

static char page_bottom[] =
#include "bottom.html.h"
;

/* ---------------------------------------------------------------------- */

static void vbipage(struct REQUEST *req, struct vt_page *page)
{
    char *out;
    int size,len,x,y;
    int color,lcolor,link;
    struct fmt_page pg[1];

    struct fmt_char l[W+2];
#define L (l+1)

    fmt_page(fmt,pg,page);
    size = 2*BUFSIZE;
    out = malloc(size);
    len = 0;

    len += sprintf(out+len,page_top,page->pgno,page->subno);
    for (y = 0; y < H; y++) {
	if (~pg->hid & (1 << y)) {  /* !hidden */
	    for (x = 0; x < W; ++x) {
		struct fmt_char c = pg->data[y][x];
		switch (c.ch) {
		case 0x00:
		case 0xa0:
		    c.ch = ' ';
		    break;
		case 0x7f:
		    c.ch = '*';
		    break;
		case BAD_CHAR:
		    c.ch = '?';
		    break;
		default:
		    if (c.attr & EA_GRAPHIC)
			c.ch = '#';
		    break;
		}
		L[x] = c;
	    }

	    /* delay fg and attr changes as far as possible */
	    for (x = 0; x < W; ++x)
		if (L[x].ch == ' ') {
		    L[x].fg = L[x-1].fg;
		    l[x].attr = L[x-1].attr;
		}

	    /* move fg and attr changes to prev bg change point */
	    for (x = W-1; x >= 0; x--)
		if (L[x].ch == ' ' && L[x].bg == L[x+1].bg) {
		    L[x].fg = L[x+1].fg;
		    L[x].attr = L[x+1].attr;
		}

	    /* now emit the whole line */
	    lcolor = -1; link = -1;
	    for (x = 0; x < W; ++x) {
		/* close link tags */
		if (link >= 0) {
		    if (0 == link)
			len += sprintf(out+len,"</a>");
		    link--;
		}

		/* color handling */
		color = (L[x].fg&0x0f) * 10 + (L[x].bg&0x0f);
		if (color != lcolor) {
		    if (-1 != lcolor)
			len += sprintf(out+len,"</span>");
		    len += sprintf(out+len,"<span class=\"c%02d\">",color);
		    lcolor = color;
		}

		/* check for refences to other pages */
		if (y > 0 && -1 == link && x < W-2 &&
		    isdigit(L[x].ch) &&
		    isdigit(L[x+1].ch) &&
		    isdigit(L[x+2].ch) &&
		    !isdigit(L[x+3].ch) &&
		    !isdigit(L[x-1].ch)) {
		    len += sprintf(out+len,"<a href=\"/%c%c%c/\">",
				   L[x].ch, L[x+1].ch, L[x+2].ch);
		    link = 2;
		}
		if (y > 0 && -1 == link && x < W-1 &&
		    '>' == L[x].ch &&
		    '>' == L[x+1].ch) {
		    len += sprintf(out+len,"<a href=\"/%03x/\">",
				   page->pgno+1);
		    link = 1;
		}
		if (y > 0 && -1 == link && x < W-1 &&
		    '<' == L[x].ch &&
		    '<' == L[x+1].ch) {
		    len += sprintf(out+len,"<a href=\"/%03x/\">",
				   page->pgno-1);
		    link = 1;
		}
		/* check for refences to other subpages */
		if (y > 0 && -1 == link && x < W-2 &&
		    page->subno > 0 && 
		    isdigit(L[x].ch) &&
		    '/' == L[x+1].ch &&
		    isdigit(L[x+2].ch) &&
		    !isdigit(L[x+3].ch) &&
		    !isdigit(L[x-1].ch)) {
		    if (L[x].ch == L[x+2].ch) {
			len += sprintf(out+len,"<a href=\"01.html\">");
		    } else {
			len += sprintf(out+len,"<a href=\"%02x.html\">",
				       L[x].ch+1-'0');
		    }
		    link = 2;
		}
		/* check for FastText links */
		if (page->flof && -1 == link && x<W-2 &&
		    24 == y &&
		    L[x].fg>0 &&
		    L[x].fg<8 &&
		    x>0 &&
		    !isspace(L[x].ch)) {
	            link=(L[x].fg==6?3:L[x].fg-1);
		    if(page->link[link].subno == ANY_SUB)
	            {
		        len+=sprintf(out+len,"<a href=\"/%03x/\">",
		            page->link[link].pgno);
		    }
		    else
	            {
		        len+=sprintf(out+len,"<a href=\"/%03x/%02x.html\">",
		            page->link[link].pgno,
			    page->link[link].subno);
		    }
		    link=0;
		    while((L[x+link].fg == L[x].fg) && (x+link<W))
		    {
		        link++;
		    }
		    link--;
		    if(link<1)
		    {
	                link=1;
		    }
		}
		out[len++] = L[x].ch;
	    }
	    /* close any tags + put newline */
	    if (link >= 0)
		len += sprintf(out+len,"</a>");
	    if (-1 != lcolor)
		len += sprintf(out+len,"</span>");
	    out[len++] = '\n';

	    /* check bufsize */
	    if (len + BUFSIZE > size) {
		size += BUFSIZE;
		out = realloc(out,size);
	    }
	}
    }
    len += sprintf(out+len,"%s",page_bottom);

    req->mime  = "text/html; charset=\"iso-8859-1\"";
    req->body  = out;
    req->lbody = len;
    req->free_the_mallocs = 1;
    mkheader(req,200,-1);
}

/* ---------------------------------------------------------------------- */

void buildpage(struct REQUEST *req)
{
    int pagenr, subpage;
    struct vt_page *page;

    /* style sheet */
    if (0 == strcmp(req->path,"/alevt.css")) {
	req->mime  = "text/css";
	req->body  = stylesheet;
	req->lbody = sizeof(stylesheet);
	mkheader(req,200,start);
	return;
    }

    /* about */
    if (0 == strcmp(req->path,"/about.html")) {
	req->mime  = "text/html; charset=\"iso-8859-1\"";
	req->body  = page_about;
	req->lbody = sizeof(page_about);
	mkheader(req,200,start);
	return;
    }

    /* entry page */
    if (0 == strcmp(req->path,"/")) {
	strcpy(req->path,"/100/");
	mkredirect(req);
	return;
    }

    /* page with subpages */
    if (2 == sscanf(req->path,"/%3x/%2x.html",&pagenr,&subpage)) {
	if (debug)
	    fprintf(stderr,"trying %03x/%02x\n",pagenr,subpage);
	page = vbi->cache->op->get(vbi->cache,pagenr,subpage);
	if (NULL != page) {
	    vbipage(req,page);
	    return;
	}
	mkerror(req,404,1);
	return;
    }

    /* ... without subpage */
    if (1 == sscanf(req->path,"/%3x/",&pagenr)) {
	if (debug)
	    fprintf(stderr,"trying %03x\n",pagenr);
	page = vbi->cache->op->get(vbi->cache,pagenr,0);
	if (NULL != page) {
	    vbipage(req,page);
	    return;
	}
	page = vbi->cache->op->get(vbi->cache,pagenr,ANY_SUB);
	if (NULL != page) {
	    sprintf(req->path,"/%03x/%02x.html",pagenr,page->subno);
	    mkredirect(req);
	    return;
	}
	mkerror(req,404,1);
	return;
    }

    /* goto form */
    if (1 == sscanf(req->path,"/goto/?p=%d",&pagenr)) {
	sprintf(req->path,"/%d/",pagenr);
	mkredirect(req);
	return;
    }
    
    mkerror(req,404,1);
    return;
}
