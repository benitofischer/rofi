/* simpleswitcher

MIT/X11 License
Copyright (c) 2012 Sean Pringle <sean.pringle@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#define _GNU_SOURCE
#include <X11/X.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xmd.h>
#include <X11/Xutil.h>
#include <X11/Xproto.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <X11/Xft/Xft.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <ctype.h>
#include <math.h>
#include <signal.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <X11/extensions/Xinerama.h>

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define NEAR(a,o,b) ((b) > (a)-(o) && (b) < (a)+(o))
#define OVERLAP(a,b,c,d) (((a)==(c) && (b)==(d)) || MIN((a)+(b), (c)+(d)) - MAX((a), (c)) > 0)
#define INTERSECT(x,y,w,h,x1,y1,w1,h1) (OVERLAP((x),(w),(x1),(w1)) && OVERLAP((y),(h),(y1),(h1)))

void* allocate(unsigned long bytes)
{
	void *ptr = malloc(bytes);
	if (!ptr)
	{
		fprintf(stderr, "malloc failed!\n");
		exit(EXIT_FAILURE);
	}
	return ptr;
}
void* allocate_clear(unsigned long bytes)
{
	void *ptr = allocate(bytes);
	memset(ptr, 0, bytes);
	return ptr;
}
void* reallocate(void *ptr, unsigned long bytes)
{
	ptr = realloc(ptr, bytes);
	if (!ptr)
	{
		fprintf(stderr, "realloc failed!\n");
		exit(EXIT_FAILURE);
	}
	return ptr;
}

void catch_exit(int sig)
{
	while (0 < waitpid(-1, NULL, WNOHANG));
}

// cli arg handling
int find_arg(int argc, char *argv[], char *key)
{
	int i; for (i = 0; i < argc && strcasecmp(argv[i], key); i++);
	return i < argc ? i: -1;
}
char* find_arg_str(int argc, char *argv[], char *key, char* def)
{
	int i = find_arg(argc, argv, key);
	return (i > 0 && i < argc-1) ? argv[i+1]: def;
}
int find_arg_opts(int argc, char *argv[], char *key, char **list, int count)
{
	char *s = find_arg_str(argc, argv, key, NULL);
	int i; for (i = 0; i < count; i++) if (s && !strcasecmp(s, list[i])) return i;
	return -1;
}
int find_arg_int(int argc, char *argv[], char *key, int def)
{
	int i = find_arg(argc, argv, key);
	return (i > 0 && i < argc-1) ? strtol(argv[i+1], NULL, 10): def;
}

unsigned int NumlockMask = 0;
Display *display;

static int (*xerror)(Display *, XErrorEvent *);

#define ATOM_ENUM(x) x
#define ATOM_CHAR(x) #x

#define EWMH_ATOMS(X) \
	X(_NET_SUPPORTING_WM_CHECK),\
	X(_NET_CLIENT_LIST),\
	X(_NET_CLIENT_LIST_STACKING),\
	X(_NET_NUMBER_OF_DESKTOPS),\
	X(_NET_CURRENT_DESKTOP),\
	X(_NET_DESKTOP_GEOMETRY),\
	X(_NET_DESKTOP_VIEWPORT),\
	X(_NET_WORKAREA),\
	X(_NET_ACTIVE_WINDOW),\
	X(_NET_CLOSE_WINDOW),\
	X(_NET_MOVERESIZE_WINDOW),\
	X(_NET_WM_NAME),\
	X(_NET_WM_WINDOW_TYPE),\
	X(_NET_WM_WINDOW_TYPE_DESKTOP),\
	X(_NET_WM_WINDOW_TYPE_DOCK),\
	X(_NET_WM_WINDOW_TYPE_SPLASH),\
	X(_NET_WM_WINDOW_TYPE_UTILITY),\
	X(_NET_WM_WINDOW_TYPE_TOOLBAR),\
	X(_NET_WM_WINDOW_TYPE_MENU),\
	X(_NET_WM_WINDOW_TYPE_DIALOG),\
	X(_NET_WM_WINDOW_TYPE_NORMAL),\
	X(_NET_WM_STATE),\
	X(_NET_WM_STATE_MODAL),\
	X(_NET_WM_STATE_STICKY),\
	X(_NET_WM_STATE_MAXIMIZED_VERT),\
	X(_NET_WM_STATE_MAXIMIZED_HORZ),\
	X(_NET_WM_STATE_SHADED),\
	X(_NET_WM_STATE_SKIP_TASKBAR),\
	X(_NET_WM_STATE_SKIP_PAGER),\
	X(_NET_WM_STATE_HIDDEN),\
	X(_NET_WM_STATE_FULLSCREEN),\
	X(_NET_WM_STATE_ABOVE),\
	X(_NET_WM_STATE_BELOW),\
	X(_NET_WM_STATE_DEMANDS_ATTENTION),\
	X(_NET_WM_STATE_ADD),\
	X(_NET_WM_STATE_REMOVE),\
	X(_NET_WM_STATE_TOGGLE),\
	X(_NET_WM_STRUT),\
	X(_NET_WM_STRUT_PARTIAL),\
	X(_NET_WM_DESKTOP),\
	X(_NET_SUPPORTED)

enum { EWMH_ATOMS(ATOM_ENUM), NETATOMS };
const char *netatom_names[] = { EWMH_ATOMS(ATOM_CHAR) };
Atom netatoms[NETATOMS];

#define ADD 1
#define REMOVE 0
#define TOGGLE 2

// X error handler
int oops(Display *d, XErrorEvent *ee)
{
	if (ee->error_code == BadWindow
		|| (ee->request_code == X_GrabButton && ee->error_code == BadAccess)
		|| (ee->request_code == X_GrabKey && ee->error_code == BadAccess)
		) return 0;
	fprintf(stderr, "error: request code=%d, error code=%d\n", ee->request_code, ee->error_code);
	return xerror(display, ee);
}

// usable space on a monitor
typedef struct {
	int x, y, w, h;
	int l, r, t, b;
} workarea;

// window lists
typedef struct {
	Window *array;
	void **data;
	int len;
} winlist;

winlist *cache_client;
winlist *cache_xattr;

#define winlist_ascend(l,i,w) for ((i) = 0; (i) < (l)->len && (((w) = (l)->array[i]) || 1); (i)++)
#define winlist_descend(l,i,w) for ((i) = (l)->len-1; (i) >= 0 && (((w) = (l)->array[i]) || 1); (i)--)

#define WINLIST 32

winlist* winlist_new()
{
	winlist *l = allocate(sizeof(winlist)); l->len = 0;
	l->array = allocate(sizeof(Window) * (WINLIST+1));
	l->data  = allocate(sizeof(void*) * (WINLIST+1));
	return l;
}
int winlist_append(winlist *l, Window w, void *d)
{
	if (l->len > 0 && !(l->len % WINLIST))
	{
		l->array = reallocate(l->array, sizeof(Window) * (l->len+WINLIST+1));
		l->data  = reallocate(l->data,  sizeof(void*)  * (l->len+WINLIST+1));
	}
	l->data[l->len] = d;
	l->array[l->len++] = w;
	return l->len-1;
}
void winlist_empty(winlist *l)
{
	while (l->len > 0) free(l->data[--(l->len)]);
}
void winlist_free(winlist *l)
{
	winlist_empty(l); free(l->array); free(l->data); free(l);
}
void winlist_empty_2d(winlist *l)
{
	while (l->len > 0) winlist_free(l->data[--(l->len)]);
}
int winlist_find(winlist *l, Window w)
{
	// iterate backwards. theory is: windows most often accessed will be
	// nearer the end. testing with kcachegrind seems to support this...
	int i; Window o; winlist_descend(l, i, o) if (w == o) return i;
	return -1;
}
int winlist_forget(winlist *l, Window w)
{
	int i, j;
	for (i = 0, j = 0; i < l->len; i++, j++)
	{
		l->array[j] = l->array[i];
		l->data[j]  = l->data[i];
		if (l->array[i] == w) { free(l->data[i]); j--; }
	}
	l->len -= (i-j);
	return j != i ?1:0;
}

#define CLIENTTITLE 100
#define CLIENTCLASS 50
#define CLIENTNAME 50
#define CLIENTSTATE 10

// a managable window
typedef struct {
	Window window, trans;
	XWindowAttributes xattr;
	char title[CLIENTTITLE], class[CLIENTCLASS], name[CLIENTNAME];
	int states;
	Atom state[CLIENTSTATE], type;
	workarea monitor;
} client;

// allocate a pixel value for an X named color
unsigned int color_get(Display *d, const char *name)
{
	XColor color;
	Colormap map = DefaultColormap(d, DefaultScreen(d));
	return XAllocNamedColor(d, map, name, &color, &color) ? color.pixel: None;
}

// find mouse pointer location
int pointer_get(Window root, int *x, int *y)
{
	*x = 0; *y = 0;
	Window rr, cr; int rxr, ryr, wxr, wyr; unsigned int mr;
	if (XQueryPointer(display, root, &rr, &cr, &rxr, &ryr, &wxr, &wyr, &mr))
	{
		*x = rxr; *y = ryr;
		return 1;
	}
	return 0;
}

// XGetWindowAttributes with caching
XWindowAttributes* window_get_attributes(Window w)
{
	int idx = winlist_find(cache_xattr, w);
	if (idx < 0)
	{
		XWindowAttributes *cattr = allocate(sizeof(XWindowAttributes));
		if (XGetWindowAttributes(display, w, cattr))
		{
			winlist_append(cache_xattr, w, cattr);
			return cattr;
		}
		free(cattr);
		return NULL;
	}
	return cache_xattr->data[idx];
}

// retrieve a property of any type from a window
int window_get_prop(Window w, Atom prop, Atom *type, int *items, void *buffer, int bytes)
{
	Atom _type; if (!type) type = &_type;
	int _items; if (!items) items = &_items;
	int format; unsigned long nitems, nbytes; unsigned char *ret = NULL;
	memset(buffer, 0, bytes);

	if (XGetWindowProperty(display, w, prop, 0, bytes/4, False, AnyPropertyType, type,
		&format, &nitems, &nbytes, &ret) == Success && ret && *type != None && format)
	{
		if (format ==  8) memmove(buffer, ret, MIN(bytes, nitems));
		if (format == 16) memmove(buffer, ret, MIN(bytes, nitems * sizeof(short)));
		if (format == 32) memmove(buffer, ret, MIN(bytes, nitems * sizeof(long)));
		*items = (int)nitems; XFree(ret);
		return 1;
	}
	return 0;
}

// retrieve a text property from a window
// technically we could use window_get_prop(), but this is better for character set support
char* window_get_text_prop(Window w, Atom atom)
{
	XTextProperty prop; char *res = NULL;
	char **list = NULL; int count;
	if (XGetTextProperty(display, w, &prop, atom) && prop.value && prop.nitems)
	{
		if (prop.encoding == XA_STRING)
		{
			res = allocate(strlen((char*)prop.value)+1);
			strcpy(res, (char*)prop.value);
		}
		else
		if (XmbTextPropertyToTextList(display, &prop, &list, &count) >= Success && count > 0 && *list)
		{
			res = allocate(strlen(*list)+1);
			strcpy(res, *list);
			XFreeStringList(list);
		}
	}
	if (prop.value) XFree(prop.value);
	return res;
}

int window_get_atom_prop(Window w, Atom atom, Atom *list, int count)
{
	Atom type; int items;
	return window_get_prop(w, atom, &type, &items, list, count*sizeof(Atom)) && type == XA_ATOM ? items:0;
}

void window_set_atom_prop(Window w, Atom prop, Atom *atoms, int count)
{
	XChangeProperty(display, w, prop, XA_ATOM, 32, PropModeReplace, (unsigned char*)atoms, count);
}

int window_get_cardinal_prop(Window w, Atom atom, unsigned long *list, int count)
{
	Atom type; int items;
	return window_get_prop(w, atom, &type, &items, list, count*sizeof(unsigned long)) && type == XA_CARDINAL ? items:0;
}

// a ClientMessage
int window_send_message(Window target, Window subject, Atom atom, unsigned long protocol, unsigned long mask)
{
	XEvent e; memset(&e, 0, sizeof(XEvent));
	e.xclient.type = ClientMessage;
	e.xclient.message_type = atom;     e.xclient.window    = subject;
	e.xclient.data.l[0]    = protocol; e.xclient.data.l[1] = CurrentTime;
	e.xclient.send_event   = True;     e.xclient.format    = 32;
	int r = XSendEvent(display, target, False, mask, &e) ?1:0;
	XFlush(display);
	return r;
}

// find the dimensions of the monitor displaying point x,y
void monitor_dimensions(Screen *screen, int x, int y, workarea *mon)
{
	memset(mon, 0, sizeof(workarea));
	mon->w = WidthOfScreen(screen);
	mon->h = HeightOfScreen(screen);

	// locate the current monitor
	if (XineramaIsActive(display))
	{
		int monitors, i;
		XineramaScreenInfo *info = XineramaQueryScreens(display, &monitors);
		if (info) for (i = 0; i < monitors; i++)
		{
			if (INTERSECT(x, y, 1, 1, info[i].x_org, info[i].y_org, info[i].width, info[i].height))
			{
				mon->x = info[i].x_org; mon->y = info[i].y_org;
				mon->w = info[i].width; mon->h = info[i].height;
				break;
			}
		}
		XFree(info);
	}
}

// determine which monitor holds the active window, or failing that the mouse pointer
void monitor_active(Screen *screen, workarea *mon)
{
	Window root = RootWindow(display, XScreenNumberOfScreen(screen));

	unsigned long id; Atom type; int count;
	if (window_get_prop(root, netatoms[_NET_ACTIVE_WINDOW], &type, &count, &id, 1)
		&& type == XA_WINDOW && count > 0)
	{
		XWindowAttributes *attr = window_get_attributes(id);
		monitor_dimensions(screen, attr->x, attr->y, mon);
		return;
	}
	int x, y;
	if (pointer_get(root, &x, &y))
	{
		monitor_dimensions(screen, x, y, mon);
		return;
	}
	monitor_dimensions(screen, 0, 0, mon);
}

// _NET_WM_STATE_*
int client_has_state(client *c, Atom state)
{
	int i; for (i = 0; i < c->states; i++) if (c->state[i] == state) return 1;
	return 0;
}

// collect info on any window
// doesn't have to be a window we'll end up managing
client* window_client(Window win)
{
	if (win == None) return NULL;
	int idx = winlist_find(cache_client, win);
	if (idx >= 0) return cache_client->data[idx];

	// if this fails, we're up that creek
	XWindowAttributes *attr = window_get_attributes(win);
	if (!attr) return NULL;

	client *c = allocate_clear(sizeof(client));
	c->window = win;
	// copy xattr so we don't have to care when stuff is freed
	memmove(&c->xattr, attr, sizeof(XWindowAttributes));
	XGetTransientForHint(display, win, &c->trans);

	c->states = window_get_atom_prop(win, netatoms[_NET_WM_STATE], c->state, CLIENTSTATE);
	window_get_atom_prop(win, netatoms[_NET_WM_WINDOW_TYPE], &c->type, 1);

	if (c->type == None) c->type = (c->trans != None)
		// trasients default to dialog
		? netatoms[_NET_WM_WINDOW_TYPE_DIALOG]
		// non-transients default to normal
		: netatoms[_NET_WM_WINDOW_TYPE_NORMAL];

	char *name;
	if ((name = window_get_text_prop(c->window, netatoms[_NET_WM_NAME])) && name)
	{
		snprintf(c->title, CLIENTTITLE, "%s", name);
		free(name);
	}
	else
	if (XFetchName(display, c->window, &name))
	{
		snprintf(c->title, CLIENTTITLE, "%s", name);
		XFree(name);
	}
	XClassHint chint;
	if (XGetClassHint(display, c->window, &chint))
	{
		snprintf(c->class, CLIENTCLASS, "%s", chint.res_class);
		snprintf(c->name, CLIENTNAME, "%s", chint.res_name);
		XFree(chint.res_class); XFree(chint.res_name);
	}
	monitor_dimensions(c->xattr.screen, c->xattr.x, c->xattr.y, &c->monitor);
	winlist_append(cache_client, c->window, c);
	return c;
}

#define MENUXFTFONT "mono-14"
#define MENUWIDTH 50
#define MENULINES 25
#define MENUFG "#cccccc"
#define MENUBG "#222222"
#define MENUHLFG "#ffffff"
#define MENUHLBG "#005577"

char *config_menu_font, *config_menu_fg, *config_menu_bg, *config_menu_hlfg, *config_menu_hlbg;
unsigned int config_menu_width, config_menu_lines, config_focus_mode, config_raise_mode, config_window_placement;

// built-in filterable popup menu list
struct localmenu {
	Window window;
	GC gc;
	Pixmap canvas;
	XftFont *font;
	XftColor *color;
	XftDraw *draw;
	XftColor fg, bg, hlfg, hlbg;
	unsigned long xbg;
	char **lines, **filtered;
	int done, max_lines, num_lines, input_size, line_height;
	int current, width, height, horz_pad, vert_pad, offset;
	char *input, *selected, *manual;
	XIM xim;
	XIC xic;
};

// redraw the popup menu window
void menu_draw(struct localmenu *my)
{
	int i, n;

	// draw text input bar
	XftDrawRect(my->draw, &my->bg, 0, 0, my->width, my->height);
	XftDrawStringUtf8(my->draw, &my->fg, my->font, my->horz_pad, my->vert_pad+my->line_height-my->font->descent, (unsigned char*)my->input, strlen(my->input));

	// filter lines by current input text
	memset(my->filtered, 0, sizeof(char*) * (my->num_lines+1));
	for (i = 0, n = 0; my->lines[i]; i++)
	{
		if (!my->offset || strcasestr(my->lines[i], my->input))
			my->filtered[n++] = my->lines[i];
	}
	// vertical bounds of highlight bar
	my->current = MAX(0, MIN(my->current, n-1));
	for (i = 0; my->filtered[i]; i++)
	{
		XftColor fg = my->fg;
		// vertical position of *top* of current line
		int y = my->vert_pad+(my->line_height*(i+1));
		// http://en.wikipedia.org/wiki/Typeface#Font_metrics
		int font_baseline = y + my->line_height - my->font->descent -1;
		// are we highlighting this line?
		if (i == my->current)
		{
			fg = my->hlfg;
			XftDrawRect(my->draw, &my->hlbg, my->horz_pad, y, my->width-(my->horz_pad*2), my->line_height);
		}
		XftDrawStringUtf8(my->draw, &fg, my->font, my->horz_pad, font_baseline, (unsigned char*)my->filtered[i], strlen(my->filtered[i]));
	}
	// double buffering
	XCopyArea(display, my->canvas, my->window, my->gc, 0, 0, my->width, my->height, 0, 0);
}

// handle popup menu text input for filtering
void menu_key(struct localmenu *my, XEvent *ev)
{
	char pad[32]; KeySym key; Status stat;
	int len = XmbLookupString(my->xic, &ev->xkey, pad, sizeof(pad), &key, &stat);
	if (stat == XBufferOverflow) return;
	pad[len] = 0;

	key = XkbKeycodeToKeysym(display, ev->xkey.keycode, 0, 0);

	if (key == XK_Escape)
		my->done = 1;
	else
	if (key == XK_BackSpace)
	{
		if (my->offset > 0)
			my->input[--(my->offset)] = 0;
	}
	else
	if (key == XK_Up)
		my->current = (my->current == 0 ? my->max_lines-1: my->current-1);
	else
	if (key == XK_Down || key == XK_Tab)
		my->current = (my->current == my->max_lines-1 ? 0: my->current+1);
	else
	if (key == XK_Return)
	{
		if (my->filtered[my->current])
			my->selected = my->filtered[my->current];
		else
		if (my->manual)
			strcpy(my->manual, my->input);
		my->done = 1;
	}
	else
	if (!iscntrl(*pad) && my->offset < my->input_size-1)
	{
		my->input[my->offset++] = *pad;
		my->input[my->offset] = 0;
	}
	menu_draw(my);
}

// take over keyboard for popup menu
int menu_grab(struct localmenu *my)
{
	int i;
	for (i = 0; i < 1000; i++)
	{
		if (XGrabKeyboard(display, my->window, True, GrabModeAsync, GrabModeAsync, CurrentTime) == GrabSuccess)
			return 1;
		usleep(1000);
	}
	return 0;
}

// menu
int menu(Window root, char **lines, char *manual)
{
	int i, l, scr;
	struct localmenu _my, *my = &_my;

	XWindowAttributes *attr = window_get_attributes(root);
	workarea mon; monitor_active(attr->screen, &mon);
	scr = XScreenNumberOfScreen(attr->screen);

	// this never fails, afaics. we get some sort of font, no matter what
	my->font = XftFontOpenName(display, scr, config_menu_font);
	XftColorAllocName(display, DefaultVisual(display, scr), DefaultColormap(display, scr), config_menu_fg, &my->fg);
	XftColorAllocName(display, DefaultVisual(display, scr), DefaultColormap(display, scr), config_menu_bg, &my->bg);
	XftColorAllocName(display, DefaultVisual(display, scr), DefaultColormap(display, scr), config_menu_hlfg, &my->hlfg);
	XftColorAllocName(display, DefaultVisual(display, scr), DefaultColormap(display, scr), config_menu_hlbg, &my->hlbg);
	my->line_height = my->font->ascent + my->font->descent +2; // +2 pixel extra line spacing

	for (l = 0, i = 0; lines[i]; i++) l = MAX(l, strlen(lines[i]));

	my->lines       = lines;
	my->num_lines   = i;
	my->max_lines   = MIN(config_menu_lines, my->num_lines);
	my->input_size  = MAX(l, 100);
	my->filtered    = allocate_clear(sizeof(char*) * (my->num_lines+1));
	my->input       = allocate_clear((my->input_size+1)*3); // utf8 in copied line
	my->current     = 0; // index of currently highlighted line
	my->offset      = 0; // length of text in input buffer
	my->done        = 0; // bailout flag
	my->horz_pad    = 5; // horizontal padding
	my->vert_pad    = 5; // vertical padding
	my->width       = (mon.w/100)*config_menu_width;
	my->height      = ((my->line_height) * (my->max_lines+1)) + (my->vert_pad*2);
	my->xbg         = color_get(display, config_menu_bg);
	my->selected    = NULL;
	my->manual      = manual;

	int x = mon.x + ((mon.w - my->width)/2);
	int y = mon.y + (mon.h/2) - (my->height/2);

	my->window = XCreateSimpleWindow(display, root, x, y, my->width, my->height, 0, my->xbg, my->xbg);
	// make it an unmanaged window
	window_set_atom_prop(my->window, netatoms[_NET_WM_STATE], &netatoms[_NET_WM_STATE_ABOVE], 1);
	window_set_atom_prop(my->window, netatoms[_NET_WM_WINDOW_TYPE], &netatoms[_NET_WM_WINDOW_TYPE_DOCK], 1);
	XSelectInput(display, my->window, ExposureMask|KeyPressMask);

	// drawing environment
	my->gc     = XCreateGC(display, my->window, 0, 0);
	my->canvas = XCreatePixmap(display, root, my->width, my->height, DefaultDepth(display, scr));
	my->draw   = XftDrawCreate(display, my->canvas, DefaultVisual(display, scr), DefaultColormap(display, scr));

	// input keymap->charmap handling
	my->xim = XOpenIM(display, NULL, NULL, NULL);
	my->xic = XCreateIC(my->xim, XNInputStyle, XIMPreeditNothing | XIMStatusNothing, XNClientWindow, my->window, XNFocusWindow, my->window, NULL);

	menu_draw(my);
	XMapRaised(display, my->window);
	if (!menu_grab(my))
	{
		fprintf(stderr, "cannot grab keyboard!\n");
		return my->max_lines;
	}
	menu_draw(my);
	// main event loop
	for(;!my->done;)
	{
		XEvent ev;
		XNextEvent(display, &ev);
		if (ev.type == Expose)
			menu_draw(my);
		else
		if (ev.type == KeyPress)
			menu_key(my, &ev);
	}
	free(my->filtered);
	XftDrawDestroy(my->draw);
	XFreeGC(display, my->gc);
	XftFontClose(display, my->font);
	XUngrabKeyboard(display, CurrentTime);
	free(my->input);

	if (my->selected)
		for (i = 0; my->lines[i]; i++)
			if (my->lines[i] == my->selected)
				return i;
	return -1;
}

#define ALLWINDOWS 1
#define DESKTOPWINDOWS 2

#define FORK 1
#define NOFORK 2

void run_switcher(Window root, int mode, int fmode)
{
	// TODO: this whole function is messy. build a nicer solution
	char pattern[50], **list = NULL;
	int i, classfield = 0, plen = 0, lines = 0;
	unsigned long desktops = 0, current_desktop = 0;
	Window w; client *c;

	// windows we actually display. may be slightly different to _NET_CLIENT_LIST_STACKING
	// if we happen to have a window destroyed while we're working...
	winlist *ids = winlist_new();

	window_get_cardinal_prop(root, netatoms[_NET_CURRENT_DESKTOP], &current_desktop, 1);

	// find window list
	Atom type; int nwins; unsigned long *wins = allocate_clear(sizeof(unsigned long) * 100);
	if (window_get_prop(root, netatoms[_NET_CLIENT_LIST_STACKING], &type, &nwins, wins, 100 * sizeof(unsigned long))
		&& type == XA_WINDOW)
	{
		// calc widths of fields
		for (i = nwins-1; i > -1; i--)
		{
			if ((c = window_client(wins[i])))
			{
				if (mode == DESKTOPWINDOWS)
				{
					unsigned long wmdesktop = 0;
					window_get_cardinal_prop(c->window, netatoms[_NET_WM_DESKTOP], &wmdesktop, 1);
					if (wmdesktop != current_desktop) continue;
				}
				classfield = MAX(classfield, strlen(c->class));
				winlist_append(ids, c->window, NULL);
			}
		}

		// build line sprintf pattern
		if (mode == ALLWINDOWS)
		{
			window_get_cardinal_prop(root, netatoms[_NET_NUMBER_OF_DESKTOPS], &desktops, 1);
			plen += sprintf(pattern+plen, "%%-%ds  ", desktops < 10 ? 1: 2);
		}
		plen += sprintf(pattern+plen, "%%-%ds   %%s", MAX(5, classfield));
		list = allocate_clear(sizeof(char*) * (ids->len+1)); lines = 0;

		// build the actual list
		winlist_ascend(ids, i, w)
		{
			if ((c = window_client(w)))
			{
				// final line format
				unsigned long wmdesktop; char desktop[5]; desktop[0] = 0;
				char *line = allocate(strlen(c->title) + strlen(c->class) + classfield + 50);
				if (mode == ALLWINDOWS)
				{
					// find client's desktop. this is zero-based, so we adjust by since most
					// normal people don't think like this :-)
					window_get_cardinal_prop(c->window, netatoms[_NET_WM_DESKTOP], &wmdesktop, 1);
					if (wmdesktop < 0xFFFFFFFF) sprintf(desktop, "%d", (int)wmdesktop+1);
					sprintf(line, pattern, desktop, c->class, c->title);
				}
				else	sprintf(line, pattern, c->class, c->title);
				list[lines++] = line;
			}
		}
		if (fmode == NOFORK || !fork())
		{
			// we fork because it's technically possible to have multiple window
			// lists up at once on a zaphod multihead X setup.
			// this also happens to isolate the Xft font stuff in a child process
			// that gets cleaned up every time. that library shows some valgrind
			// strangeness...
			display = XOpenDisplay(0);
			XSync(display, True);
			int n = menu(root, list, NULL);
			if (n >= 0 && list[n])
			{
				if (mode == ALLWINDOWS)
				{
					// TODO: get rid of strtol
					window_send_message(root, root, netatoms[_NET_CURRENT_DESKTOP], strtol(list[n], NULL, 10)-1,
						SubstructureNotifyMask | SubstructureRedirectMask);
					XSync(display, False);
				}
				window_send_message(root, ids->array[n], netatoms[_NET_ACTIVE_WINDOW], 2, // 2 = pager
					SubstructureNotifyMask | SubstructureRedirectMask);
			}
			exit(EXIT_SUCCESS);
		}
		for (i = 0; i < lines; i++) free(list[i]);
		free(list);
	}
	free(wins);
	winlist_free(ids);
}

unsigned int all_windows_modmask; KeySym all_windows_keysym;
unsigned int desktop_windows_modmask; KeySym desktop_windows_keysym;

// KeyPress event
void handle_keypress(XEvent *ev)
{
	KeySym key = XkbKeycodeToKeysym(display, ev->xkey.keycode, 0, 0);

	if ((all_windows_modmask == AnyModifier || ev->xkey.state & all_windows_modmask) && key == all_windows_keysym)
		run_switcher(ev->xany.window, ALLWINDOWS, FORK);

	if ((desktop_windows_modmask == AnyModifier || ev->xkey.state & desktop_windows_modmask) && key == desktop_windows_keysym)
		run_switcher(ev->xany.window, DESKTOPWINDOWS, FORK);
}

// convert a Mod+key arg to mod mask and keysym
void parse_key(char *combo, unsigned int *mod, KeySym *key)
{
	unsigned int modmask = 0;
	if (strcasestr(combo, "shift"))   modmask |= ShiftMask;
	if (strcasestr(combo, "control")) modmask |= ControlMask;
	if (strcasestr(combo, "mod1"))    modmask |= Mod1Mask;
	if (strcasestr(combo, "mod2"))    modmask |= Mod2Mask;
	if (strcasestr(combo, "mod3"))    modmask |= Mod3Mask;
	if (strcasestr(combo, "mod4"))    modmask |= Mod4Mask;
	if (strcasestr(combo, "mod5"))    modmask |= Mod5Mask;
	*mod = modmask ? modmask: AnyModifier;

	char i = strlen(combo);
	while (i > 0 && !strchr("-+", combo[i-1])) i--;

	KeySym sym = XStringToKeysym(combo+i);
	if (sym == NoSymbol || (!modmask && (strchr(combo, '-') || strchr(combo, '+'))))
	{
		fprintf(stderr, "sorry, cannot understand key combination: %s\n", combo);
		exit(EXIT_FAILURE);
	}
	*key = sym;
}

// bind a key combination on a root window, compensating for Lock* states
void grab_key(Window root, unsigned int modmask, KeySym key)
{
	KeyCode keycode = XKeysymToKeycode(display, key);
	XUngrabKey(display, keycode, AnyModifier, root);

	if (modmask != AnyModifier)
	{
		// bind to combinations of mod and lock masks, so caps and numlock don't confuse people
		XGrabKey(display, keycode, modmask, root, True, GrabModeAsync, GrabModeAsync);
		XGrabKey(display, keycode, modmask|LockMask, root, True, GrabModeAsync, GrabModeAsync);
		if (NumlockMask)
		{
			XGrabKey(display, keycode, modmask|NumlockMask, root, True, GrabModeAsync, GrabModeAsync);
			XGrabKey(display, keycode, modmask|NumlockMask|LockMask, root, True, GrabModeAsync, GrabModeAsync);
		}
	}
	else
	{
		// nice simple single key bind
		XGrabKey(display, keycode, AnyModifier, root, True, GrabModeAsync, GrabModeAsync);
	}
}

int main(int argc, char *argv[])
{
	int i, j, scr;

	// catch help request
	if (find_arg(argc, argv, "-help") >= 0
		|| find_arg(argc, argv, "--help") >= 0
		|| find_arg(argc, argv, "-h") >= 0)
	{
		fprintf(stderr, "See the man page or visit http://github.com/seanpringle/simpleswitcher\n");
		return EXIT_FAILURE;
	}
	if(!(display = XOpenDisplay(0)))
	{
		fprintf(stderr, "cannot open display!\n");
		return EXIT_FAILURE;
	}
	signal(SIGCHLD, catch_exit);
	XSync(display, False); xerror = XSetErrorHandler(oops); XSync(display, False);

	// determine numlock mask so we can bind on keys with and without it
	XModifierKeymap *modmap = XGetModifierMapping(display);
	for (i = 0; i < 8; i++)
		for (j = 0; j < (int)modmap->max_keypermod; j++)
			if (modmap->modifiermap[i*modmap->max_keypermod+j] == XKeysymToKeycode(display, XK_Num_Lock))
				NumlockMask = (1<<i);
	XFreeModifiermap(modmap);

	int ac = argc; char **av = argv;
	cache_client = winlist_new();
	cache_xattr  = winlist_new();

	// X atom values
	for (i = 0; i < NETATOMS; i++) netatoms[i] = XInternAtom(display, netatom_names[i], False);

	config_menu_width = find_arg_int(ac, av, "-width", MENUWIDTH);
	config_menu_lines = find_arg_int(ac, av, "-lines", MENULINES);
	config_menu_font  = find_arg_str(ac, av, "-font", MENUXFTFONT);
	config_menu_fg    = find_arg_str(ac, av, "-fg", MENUFG);
	config_menu_bg    = find_arg_str(ac, av, "-bg", MENUBG);
	config_menu_hlfg  = find_arg_str(ac, av, "-hlfg", MENUHLFG);
	config_menu_hlbg  = find_arg_str(ac, av, "-hlbg", MENUHLBG);

	// flags to run immediately and exit
	if (find_arg(ac, av, "-now") >= 0)
	{
		run_switcher(RootWindow(display, DefaultScreen(display)), ALLWINDOWS, NOFORK);
		exit(EXIT_SUCCESS);
	}
	if (find_arg(ac, av, "-dnow") >= 0)
	{
		run_switcher(RootWindow(display, DefaultScreen(display)), DESKTOPWINDOWS, NOFORK);
		exit(EXIT_SUCCESS);
	}
	// in background mode from here on

	// key combination to display all windows from all desktops
	parse_key(find_arg_str(ac, av, "-key", "F12"), &all_windows_modmask, &all_windows_keysym);

	// key combination to display only window on the current desktop
	parse_key(find_arg_str(ac, av, "-dkey", "F11"), &desktop_windows_modmask, &desktop_windows_keysym);

	// bind key combos
	for (scr = 0; scr < ScreenCount(display); scr++)
	{
		Window root = RootWindow(display, scr);
		grab_key(root, all_windows_modmask, all_windows_keysym);
		grab_key(root, desktop_windows_modmask, desktop_windows_keysym);
	}

	XEvent ev;
	for (;;)
	{
		// caches only live for a single event
		winlist_empty(cache_xattr);
		winlist_empty(cache_client);

		// block and wait for something
		XNextEvent(display, &ev);
		if (ev.xany.window == None) continue;

		if (ev.type == KeyPress) handle_keypress(&ev);
	}
	return EXIT_SUCCESS;
}