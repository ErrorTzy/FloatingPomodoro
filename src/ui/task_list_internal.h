#pragma once

#include <gtk/gtk.h>

#include "app/app_state.h"

struct _TaskRowControls {
  AppState *state;
  PomodoroTask *task;
  GtkWidget *repeat_label;
  GtkWidget *count_label;
  GtkWidget *title_label;
  GtkWidget *title_entry;
  GtkWidget *edit_button;
  gboolean title_edit_active;
  gboolean title_edit_has_focus;
  gint64 title_edit_started_at;
};

guint task_list_calculate_cycle_minutes(guint cycles);
char *task_list_format_minutes(guint minutes);
char *task_list_format_cycle_summary(guint cycles);

void task_list_update_current_summary(AppState *state);
void task_list_append_row(AppState *state, GtkWidget *list, PomodoroTask *task);

void on_task_edit_clicked(GtkButton *button, gpointer user_data);
void on_task_title_activate(GtkEntry *entry, gpointer user_data);
void on_task_title_focus_changed(GObject *object,
                                 GParamSpec *pspec,
                                 gpointer user_data);
