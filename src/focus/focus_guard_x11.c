#include "focus/focus_guard_x11.h"

#include <gdk/x11/gdkx.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>

G_GNUC_BEGIN_IGNORE_DEPRECATIONS

static char *
focus_guard_x11_get_window_title(GdkDisplay *display,
                                 Display *xdisplay,
                                 Window window)
{
  if (display == NULL || xdisplay == NULL || window == 0) {
    return NULL;
  }

  Atom net_wm_name = XInternAtom(xdisplay, "_NET_WM_NAME", True);
  Atom utf8_string = XInternAtom(xdisplay, "UTF8_STRING", True);

  if (net_wm_name != None && utf8_string != None) {
    Atom actual_type = None;
    int actual_format = 0;
    unsigned long nitems = 0;
    unsigned long bytes_after = 0;
    unsigned char *prop = NULL;

    gdk_x11_display_error_trap_push(display);
    int status = XGetWindowProperty(xdisplay,
                                    window,
                                    net_wm_name,
                                    0,
                                    1024,
                                    False,
                                    utf8_string,
                                    &actual_type,
                                    &actual_format,
                                    &nitems,
                                    &bytes_after,
                                    &prop);
    int error = gdk_x11_display_error_trap_pop(display);

    if (error == 0 && status == Success && prop != NULL) {
      char *title = g_strndup((const char *)prop, (gsize)nitems);
      XFree(prop);
      if (title != NULL && *title != '\0') {
        return title;
      }
      g_free(title);
    }
    if (prop != NULL) {
      XFree(prop);
    }
  }

  gdk_x11_display_error_trap_push(display);
  char *fallback = NULL;
  int fetch_ok = XFetchName(xdisplay, window, &fallback);
  int error = gdk_x11_display_error_trap_pop(display);

  if (error == 0 && fetch_ok && fallback != NULL) {
    char *title = g_strdup(fallback);
    XFree(fallback);
    if (title != NULL && *title != '\0') {
      return title;
    }
    g_free(title);
  } else if (fallback != NULL) {
    XFree(fallback);
  }

  return NULL;
}

gboolean
focus_guard_x11_get_active_app(char **app_name_out, char **title_out)
{
  if (app_name_out != NULL) {
    *app_name_out = NULL;
  }
  if (title_out != NULL) {
    *title_out = NULL;
  }

  GdkDisplay *display = gdk_display_get_default();
  if (display == NULL || !GDK_IS_X11_DISPLAY(display)) {
    return FALSE;
  }

  Display *xdisplay = gdk_x11_display_get_xdisplay(display);
  if (xdisplay == NULL) {
    return FALSE;
  }

  Atom active_atom = XInternAtom(xdisplay, "_NET_ACTIVE_WINDOW", True);
  if (active_atom == None) {
    return FALSE;
  }

  Window root = DefaultRootWindow(xdisplay);
  Atom actual_type = None;
  int actual_format = 0;
  unsigned long nitems = 0;
  unsigned long bytes_after = 0;
  unsigned char *prop = NULL;

  int status = XGetWindowProperty(xdisplay,
                                  root,
                                  active_atom,
                                  0,
                                  1,
                                  False,
                                  AnyPropertyType,
                                  &actual_type,
                                  &actual_format,
                                  &nitems,
                                  &bytes_after,
                                  &prop);
  if (status != Success || prop == NULL || nitems == 0) {
    if (prop != NULL) {
      XFree(prop);
    }
    return FALSE;
  }

  Window active_window = *(Window *)prop;
  XFree(prop);

  if (active_window == 0) {
    return FALSE;
  }

  char *res_name = NULL;
  char *res_class = NULL;
  XClassHint class_hint = {0};

  gdk_x11_display_error_trap_push(display);
  int got_class = XGetClassHint(xdisplay, active_window, &class_hint);
  int error = gdk_x11_display_error_trap_pop(display);

  if (error == 0 && got_class) {
    if (class_hint.res_name != NULL) {
      res_name = g_strdup(class_hint.res_name);
      XFree(class_hint.res_name);
    }
    if (class_hint.res_class != NULL) {
      res_class = g_strdup(class_hint.res_class);
      XFree(class_hint.res_class);
    }
  } else if (error != 0) {
    /* Window was destroyed between getting active window and querying it */
    return FALSE;
  }

  char *title = focus_guard_x11_get_window_title(display, xdisplay, active_window);
  char *app_name = NULL;
  if (res_class != NULL && *res_class != '\0') {
    app_name = res_class;
    res_class = NULL;
  } else if (res_name != NULL && *res_name != '\0') {
    app_name = res_name;
    res_name = NULL;
  } else if (title != NULL && *title != '\0') {
    app_name = g_strdup(title);
  }

  g_free(res_name);
  g_free(res_class);

  if (app_name_out != NULL) {
    *app_name_out = app_name;
  } else {
    g_free(app_name);
  }

  if (title_out != NULL) {
    *title_out = title;
  } else {
    g_free(title);
  }

  return app_name != NULL;
}

G_GNUC_END_IGNORE_DEPRECATIONS
