#pragma once

#include <gtk/gtk.h>

#include "app/app_state.h"

void overlay_window_create(GtkApplication *app, AppState *state);
void overlay_window_update(AppState *state);
