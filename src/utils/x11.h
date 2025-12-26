#pragma once

#include <gtk/gtk.h>

void x11_window_set_keep_above(GtkWindow *window, gboolean above);
void x11_window_set_skip_taskbar(GtkWindow *window, gboolean skip);
void x11_window_set_skip_pager(GtkWindow *window, gboolean skip);
