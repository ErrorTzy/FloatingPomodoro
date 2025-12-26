#pragma once

#include <gtk/gtk.h>

#include "app/app_state.h"

void tray_item_create(GtkApplication *app, AppState *state);
void tray_item_update(AppState *state);
void tray_item_destroy(AppState *state);
