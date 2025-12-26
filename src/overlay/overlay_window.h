#pragma once

#include <gtk/gtk.h>

#include "app/app_state.h"

void overlay_window_create(GtkApplication *app, AppState *state);
void overlay_window_update(AppState *state);
gboolean overlay_window_is_visible(AppState *state);
void overlay_window_set_visible(AppState *state, gboolean visible);
void overlay_window_toggle_visible(AppState *state);
void overlay_window_sync_toggle_icon(AppState *state);
void overlay_window_set_warning(AppState *state, gboolean active, const char *text);
