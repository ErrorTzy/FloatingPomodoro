#include "utils/x11.h"

#ifdef GDK_WINDOWING_X11
#include <gdk/x11/gdkx.h>
#include <X11/Xlib.h>
#include <string.h>

G_GNUC_BEGIN_IGNORE_DEPRECATIONS

static void
x11_send_wm_state(GtkWindow *window, gboolean add, const char *state_name)
{
  if (window == NULL || state_name == NULL) {
    return;
  }

  GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(window));
  if (surface == NULL || !GDK_IS_X11_SURFACE(surface)) {
    return;
  }

  GdkDisplay *display = gdk_surface_get_display(surface);
  if (display == NULL || !GDK_IS_X11_DISPLAY(display)) {
    return;
  }

  Display *xdisplay = gdk_x11_display_get_xdisplay(display);
  if (xdisplay == NULL) {
    return;
  }

  Window xid = gdk_x11_surface_get_xid(surface);
  if (xid == 0) {
    return;
  }

  Atom wm_state = XInternAtom(xdisplay, "_NET_WM_STATE", False);
  Atom state_atom = XInternAtom(xdisplay, state_name, False);
  if (wm_state == None || state_atom == None) {
    return;
  }

  XEvent event;
  memset(&event, 0, sizeof(event));
  event.xclient.type = ClientMessage;
  event.xclient.serial = 0;
  event.xclient.send_event = True;
  event.xclient.display = xdisplay;
  event.xclient.window = xid;
  event.xclient.message_type = wm_state;
  event.xclient.format = 32;
  event.xclient.data.l[0] = add ? 1 : 0;
  event.xclient.data.l[1] = (long)state_atom;
  event.xclient.data.l[2] = 0;
  event.xclient.data.l[3] = 1;
  event.xclient.data.l[4] = 0;

  XSendEvent(xdisplay,
             DefaultRootWindow(xdisplay),
             False,
             SubstructureRedirectMask | SubstructureNotifyMask,
             &event);
  XFlush(xdisplay);
}
#endif

void
x11_window_set_keep_above(GtkWindow *window, gboolean above)
{
#ifdef GDK_WINDOWING_X11
  x11_send_wm_state(window, above, "_NET_WM_STATE_ABOVE");
#else
  (void)window;
  (void)above;
#endif
}

void
x11_window_set_skip_taskbar(GtkWindow *window, gboolean skip)
{
#ifdef GDK_WINDOWING_X11
  if (window == NULL) {
    return;
  }

  GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(window));
  if (surface == NULL || !GDK_IS_X11_SURFACE(surface)) {
    return;
  }

  gdk_x11_surface_set_skip_taskbar_hint(surface, skip);
#else
  (void)window;
  (void)skip;
#endif
}

void
x11_window_set_skip_pager(GtkWindow *window, gboolean skip)
{
#ifdef GDK_WINDOWING_X11
  if (window == NULL) {
    return;
  }

  GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(window));
  if (surface == NULL || !GDK_IS_X11_SURFACE(surface)) {
    return;
  }

  gdk_x11_surface_set_skip_pager_hint(surface, skip);
#else
  (void)window;
  (void)skip;
#endif
}

#ifdef GDK_WINDOWING_X11
G_GNUC_END_IGNORE_DEPRECATIONS
#endif
