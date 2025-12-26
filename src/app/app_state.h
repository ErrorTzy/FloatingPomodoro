#pragma once

#include <gtk/gtk.h>

#include "core/task_store.h"

typedef struct _TaskRowControls TaskRowControls;
typedef struct _PomodoroTimer PomodoroTimer;

typedef struct {
  TaskStore *store;
  PomodoroTimer *timer;
  GtkWindow *window;
  GtkWindow *settings_window;
  GtkWindow *archived_window;
  GtkWindow *overlay_window;
  TaskRowControls *editing_controls;
  GtkWidget *task_list;
  GtkWidget *task_empty_label;
  GtkWidget *task_entry;
  GtkWidget *task_repeat_spin;
  GtkWidget *task_repeat_hint;
  GtkWidget *current_task_label;
  GtkWidget *current_task_meta;
  GtkWidget *timer_title_label;
  GtkWidget *timer_value_label;
  GtkWidget *timer_pill_label;
  GtkWidget *timer_start_button;
  GtkWidget *timer_skip_button;
  GtkWidget *timer_stop_button;
  GtkWidget *timer_focus_stat_label;
  GtkWidget *timer_break_stat_label;
} AppState;

AppState *app_state_create(GtkWindow *window, TaskStore *store);
void app_state_free(gpointer data);
