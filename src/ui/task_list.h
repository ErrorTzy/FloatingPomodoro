#pragma once

#include <gtk/gtk.h>

#include "app/app_state.h"

void task_list_refresh(AppState *state);
void task_list_save_store(AppState *state);
void task_list_update_repeat_hint(GtkSpinButton *spin, GtkWidget *label);
void task_list_on_repeat_spin_changed(GtkSpinButton *spin, gpointer user_data);
void task_list_on_add_clicked(GtkButton *button, gpointer user_data);
void task_list_on_entry_activate(GtkEntry *entry, gpointer user_data);
void task_list_on_window_pressed(GtkGestureClick *gesture,
                                 gint n_press,
                                 gdouble x,
                                 gdouble y,
                                 gpointer user_data);
