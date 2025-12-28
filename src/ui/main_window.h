#pragma once

#include <gtk/gtk.h>

#include "app/app_state.h"

void main_window_present(GtkApplication *app, gboolean autostart_launch);
void main_window_update_timer_ui(AppState *state);
