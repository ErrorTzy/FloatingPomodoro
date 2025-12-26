#include "focus/focus_guard_x11.h"

#include <gdk/x11/gdkx.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>

static char *
focus_guard_x11_get_window_title(Display *xdisplay, Window window)
{
  if (xdisplay == NULL || window == 0) {
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
    if (status == Success && prop != NULL) {
      char *title = g_strndup((const char *)prop, (gsize)nitems);
      XFree(prop);
      if (title != NULL && *title != '\0') {
        return title;
      }
      g_free(title);
    }
  }

  char *fallback = NULL;
  if (XFetchName(xdisplay, window, &fallback) && fallback != NULL) {
    char *title = g_strdup(fallback);
    XFree(fallback);
    if (title != NULL && *title != '\0') {
      return title;
    }
    g_free(title);
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

  G_GNUC_BEGIN_IGNORE_DEPRECATIONS
  Display *xdisplay = gdk_x11_display_get_xdisplay(display);
  G_GNUC_END_IGNORE_DEPRECATIONS
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
  if (XGetClassHint(xdisplay, active_window, &class_hint)) {
    if (class_hint.res_name != NULL) {
      res_name = g_strdup(class_hint.res_name);
    }
    if (class_hint.res_class != NULL) {
      res_class = g_strdup(class_hint.res_class);
    }
    if (class_hint.res_name != NULL) {
      XFree(class_hint.res_name);
    }
    if (class_hint.res_class != NULL) {
      XFree(class_hint.res_class);
    }
  }

  char *title = focus_guard_x11_get_window_title(xdisplay, active_window);
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
