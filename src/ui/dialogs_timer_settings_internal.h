#pragma once

#include <gtk/gtk.h>

#include "app/app_state.h"

typedef struct {
  AppState *state;
  GtkWindow *window;
  GtkSpinButton *focus_spin;
  GtkSpinButton *short_spin;
  GtkSpinButton *long_spin;
  GtkSpinButton *interval_spin;
  GtkCheckButton *close_to_tray_check;
  GtkCheckButton *focus_guard_global_check;
  GtkCheckButton *focus_guard_warnings_check;
  GtkSpinButton *focus_guard_interval_spin;
  GtkWidget *focus_guard_list;
  GtkWidget *focus_guard_empty_label;
  GtkWidget *focus_guard_entry;
  GtkWidget *focus_guard_active_label;
  guint focus_guard_active_source;
  char *focus_guard_last_external;
  gboolean suppress_signals;
} TimerSettingsDialog;

void focus_guard_settings_append(TimerSettingsDialog *dialog, GtkWidget *root);
void focus_guard_settings_update_controls(TimerSettingsDialog *dialog);
void focus_guard_start_active_monitor(TimerSettingsDialog *dialog);
