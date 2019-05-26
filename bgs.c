/* See LICENSE file for copyright and license details.
 *
 * To understand bgs , start reading main().
 */
#include <getopt.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <Imlib2.h>
#include <libexif/exif-data.h>
#ifdef XINERAMA
#include <X11/extensions/Xinerama.h>
#endif

#define MIN(a, b)       ((a) < (b) ? (a) : (b))
#define MAX(a, b)       ((a) > (b) ? (a) : (b))
#define LENGTH(x)       (sizeof x / sizeof x[0])

/* image modes */
enum { ModeCenter, ModeZoom, ModeScale, ModeLast };

struct Monitor {
	int x, y, w, h;
};

static int sx, sy, sw, sh;		/* screen geometry */
static unsigned int mode = ModeScale;	/* image mode */
static Bool rotate = False;
static Bool running = False;
static Bool exif = False;
static Display *dpy;
static Window root;
static int nmonitor, nimage;	/* Amount of monitors/available background
				   images */
static struct Monitor monitors[8];
static Imlib_Image images[LENGTH(monitors)];
static short rotation[LENGTH(monitors)];

static const exifflip = (1 << 2) | (1 << 4) | (1 << 5) | (1 << 7);

/* free images before exit */
void
cleanup(void) {
	int i;

	for(i = 0; i < nimage; i++) {
		imlib_context_set_image(images[i]);
		imlib_free_image_and_decache();
	}
}

void
die(const char *errstr) {
	fputs(errstr, stderr);
	exit(EXIT_FAILURE);
}

void
getatomprop(Atom atom, Atom *type, unsigned char **data) {
	int format;
	unsigned long length, after;

	XGetWindowProperty(dpy, root, atom, 0L, 1L, False, AnyPropertyType,
		type, &format, &length, &after, data);
}

/* taken from hsetroot */
Bool
set_root_atoms(Pixmap pm) {
	Atom atom_root, atom_eroot, type;
	unsigned char *data_root, *data_eroot;

	/* Doing this to cleanup the old background */
	atom_root = XInternAtom(dpy, "_XROOTMAP_ID", True);
	atom_eroot = XInternAtom(dpy, "ESETROOT_PMAP_ID", True);


	if(atom_root != None && atom_eroot != None) {
		getatomprop(atom_root, &type, &data_root);
		if(type == XA_PIXMAP) {
			getatomprop(atom_eroot, &type, &data_eroot);
			if(data_root && data_eroot && type == XA_PIXMAP &&
			  *((Pixmap *) data_root) == *((Pixmap *) data_eroot)) {
				XKillClient(dpy, *((Pixmap *) data_root));
			}
		}
	}

	/* Setting new background atoms. */
	atom_root = XInternAtom(dpy, "_XROOTPMAP_ID", False);
	atom_eroot = XInternAtom(dpy, "ESETROOT_PMAP_ID", False);

	if(atom_root == None || atom_eroot == None) {
		return False;
	}

	XChangeProperty(dpy, root, atom_root, XA_PIXMAP, 32, PropModeReplace,
		(unsigned char *) &pm, 1);
	XChangeProperty(dpy, root, atom_eroot, XA_PIXMAP, 32, PropModeReplace,
		(unsigned char *) &pm, 1);

	return True;
}

/* draw background to root */
void
drawbg(void) {
	int i, w, h, nx, ny, nh, nw, tmp;
	double factor;
	Pixmap pm;
	Imlib_Image tmpimg, buffer;
  Bool flip;

	pm = XCreatePixmap(dpy, root, sw, sh,
			   DefaultDepth(dpy, DefaultScreen(dpy)));
	if(!(buffer = imlib_create_image(sw, sh)))
		die("Error: Cannot allocate buffer.\n");
	imlib_context_set_image(buffer);
	imlib_image_fill_rectangle(0, 0, sw, sh);
	imlib_context_set_blend(1);
	for(i = 0; i < nmonitor; i++) {
		imlib_context_set_image(images[i % nimage]);
		w = imlib_image_get_width();
		h = imlib_image_get_height();
		if(!(tmpimg = imlib_clone_image()))
			die("Error: Cannot clone image.\n");
		imlib_context_set_image(tmpimg);
    if(exif) {
      flip = False;
      if ((1 << rotation[i]) & exifflip) {
        flip = True;
        rotation[i] += (rotation[i] <= 4 ? -1 : 1);
      }
      switch(rotation[i]) {
        case 8: 
          imlib_image_orientate(3);
          break;
        case 3:
          imlib_image_orientate(2);
          break;
        case 6:
          imlib_image_orientate(1);
          break;
      }
      if (flip)
        imlib_image_flip_horizontal();
      if ((rotation[i] == 6) || (rotation[i] == 8)) {
        tmp= w;
        w = h;
        h = tmp;
      }
    }
    if(rotate && ((monitors[i].w > monitors[i].h && w < h) ||
		   (monitors[i].w < monitors[i].h && w > h))) {
			imlib_image_orientate(1);
			tmp = w;
			w = h;
			h = tmp;
		}
		imlib_context_set_image(buffer);
		switch(mode) {
		case ModeCenter:
			nw = (monitors[i].w - w) / 2;
			nh = (monitors[i].h - h) / 2;
			nx = monitors[i].x + (monitors[i].w - nw) / 2;
			ny = monitors[i].y + (monitors[i].h - nh) / 2;
			break;
		case ModeZoom:
			if(((float)w / h) > ((float)monitors[i].w / monitors[i].h)) {
				nh = monitors[i].h;
				nw = nh * w / h;
				nx = monitors[i].x + (monitors[i].w - nw) / 2;
				ny = monitors[i].y;
			}
			else {
				ny = monitors[i].y + (monitors[i].h - nh) / 2;
				nx = monitors[i].x;
			}
			break;
		default: /* ModeScale */
			factor = MAX((double)w / monitors[i].w,
				     (double)h / monitors[i].h);
			nw = w / factor;
			nh = h / factor;
			nx = monitors[i].x + (monitors[i].w - nw) / 2;
			ny = monitors[i].y + (monitors[i].h - nh) / 2;
		}
		imlib_blend_image_onto_image(tmpimg, 0, 0, 0, w, h,
					     nx, ny, nw, nh);
		imlib_context_set_image(tmpimg);
		imlib_free_image();
	}
	imlib_context_set_blend(0);
	imlib_context_set_image(buffer);
	imlib_context_set_drawable(root);
	imlib_render_image_on_drawable(0, 0);
	imlib_context_set_drawable(pm);
	imlib_render_image_on_drawable(0, 0);

	if (!set_root_atoms(pm))
		fputs("Warning: Could not create atoms!\n", stderr);

	XKillClient(dpy, AllTemporary);
	XSetCloseDownMode(dpy, RetainPermanent);
	XSetWindowBackgroundPixmap(dpy, root, pm);
	XClearWindow(dpy, root);
	XFlush(dpy);
	imlib_context_set_image(buffer);
	imlib_free_image_and_decache();
}

/* update screen and/or Xinerama dimensions */
void
updategeom(void) {
#ifdef XINERAMA
	int i;
	XineramaScreenInfo *info = NULL;

	if(XineramaIsActive(dpy) &&
	   (info = XineramaQueryScreens(dpy, &nmonitor))) {
		nmonitor = MIN(nmonitor, LENGTH(monitors));
		for(i = 0; i < nmonitor; i++) {
			monitors[i].x = info[i].x_org;
			monitors[i].y = info[i].y_org;
			monitors[i].w = info[i].width;
			monitors[i].h = info[i].height;
		}
		XFree(info);
	}
	else
#endif
	{
		nmonitor = 1;
		monitors[0].x = sx;
		monitors[0].y = sy;
		monitors[0].w = sw;
		monitors[0].h = sh;
	}
}

/* main loop */
void
run(void) {
	XEvent ev;

	for(;;) {
		updategeom();
		drawbg();
		if(!running)
			break;
		imlib_flush_loaders();
		XNextEvent(dpy, &ev);
		if(ev.type == ConfigureNotify) {
			sw = ev.xconfigure.width;
			sh = ev.xconfigure.height;
			imlib_flush_loaders();
		}
	}
}

/* set up imlib and X */
void
setup(char *paths[], int c, const char *col) {
	Visual *vis;
	Colormap cm;
	XColor color;
	int i, screen;

	/* Loading images */
	for(nimage = i = 0; i < c && i < LENGTH(images); i++) {
		if((images[nimage] = imlib_load_image_without_cache(paths[i]))) {
      if (exif) {
        ExifData *edata;
        edata = exif_data_new_from_file(paths[i]);
        if (!edata) {
          fprintf(stderr, "Warning: No EXIF data was readable from file %s\n.");
          rotation[nimage] = 1;
        }
        else {
          ExifIfd ifd = EXIF_IFD_0;
          ExifEntry *entry = exif_content_get_entry(edata->ifd[ifd], EXIF_TAG_ORIENTATION);
          rotation[nimage] = exif_get_short(entry->data, exif_data_get_byte_order(edata));
       }
      }
      else
        rotation[nimage] = 1;
      nimage++;
    }
		else {
			fprintf(stderr, "Warning: Cannot load file `%s`. "
					"Ignoring.\n", paths[nimage]);
			continue;
		}
	}
	if(nimage == 0)
		die("Error: No image to draw.\n");

	/* set up X */
	screen = DefaultScreen(dpy);
	vis = DefaultVisual(dpy, screen);
	cm = DefaultColormap(dpy, screen);
	root = RootWindow(dpy, screen);
	XSelectInput(dpy, root, StructureNotifyMask);
	sx = sy = 0;
	sw = DisplayWidth(dpy, screen);
	sh = DisplayHeight(dpy, screen);

	if(!XAllocNamedColor(dpy, cm, col, &color, &color))
		die("Error: Cannot allocate color.\n");

	/* set up Imlib */
	imlib_context_set_display(dpy);
	imlib_context_set_visual(vis);
	imlib_context_set_colormap(cm);
	imlib_context_set_color(color.red, color.green, color.blue, 255);
}

int
main(int argc, char *argv[]) {
	int opt;
	const char *col = NULL;

	while((opt = getopt(argc, argv, "ecC:Rvxz")) != -1)
		switch(opt) {
      case 'e':
        exif = True;
        break;
      case 'c':
			  mode = ModeCenter;
			  break;
		  case 'C':
			  col = optarg;
			  break;
  		case 'R':
  			rotate = True;
  			break;
	  	case 'v':
		  	printf("bgs-"VERSION", © 2010 bgs engineers, "
			         "see LICENSE for details\n");
	  		return EXIT_SUCCESS;
	  	case 'x':
		  	running = True;
		  	break;
  		case 'z':
	  		mode = ModeZoom;
		  	break;
		  default:
			  die("usage: bgs [-e] [-v] [-c] [-C hex] [-z] [-R] [-x] [IMAGE]...\n");
		}
	argc -= optind;
	argv += optind;

	if(!col)
		col = "#000000";
	if(!(dpy = XOpenDisplay(NULL)))
		die("bgs: cannot open display\n");
	setup(argv, argc, col);
	run();
	cleanup();
	XCloseDisplay(dpy);
	return EXIT_SUCCESS;
}
