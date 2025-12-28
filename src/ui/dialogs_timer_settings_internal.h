#pragma once

#include <gtk/gtk.h>
#include "app/app_state.h"
#include "ui/focus_guard_settings_model.h"

typedef struct {
  AppState *state;
  GtkWindow *window;
  GtkSpinButton *focus_spin;
  GtkSpinButton *short_spin;
  GtkSpinButton *long_spin;
  GtkSpinButton *interval_spin;
  GtkCheckButton *close_to_tray_check;
  GtkCheckButton *autostart_check;
  GtkCheckButton *autostart_start_in_tray_check;
  GtkCheckButton *minimize_to_tray_check;
  GtkCheckButton *focus_guard_global_check;
  GtkCheckButton *focus_guard_warnings_check;
  GtkSpinButton *focus_guard_interval_spin;
  GtkCheckButton *focus_guard_chrome_check;
  GtkSpinButton *focus_guard_chrome_port_spin;
  GtkDropDown *focus_guard_ollama_dropdown;
  GtkButton *focus_guard_ollama_refresh_button;
  GtkWidget *focus_guard_ollama_status_label;
  GtkWidget *focus_guard_trafilatura_status_label;
  GtkWidget *focus_guard_ollama_section;
  GtkWidget *focus_guard_list;
  GtkWidget *focus_guard_empty_label;
  GtkWidget *focus_guard_entry;
  GtkWidget *focus_guard_active_label;
  guint focus_guard_active_source;
  FocusGuardSettingsModel *focus_guard_model;
  gboolean suppress_signals;
} TimerSettingsDialog;

void focus_guard_settings_append(TimerSettingsDialog *dialog,
                                 GtkWidget *focus_root,
                                 GtkWidget *chrome_root);
void focus_guard_settings_update_controls(TimerSettingsDialog *dialog);
void focus_guard_start_active_monitor(TimerSettingsDialog *dialog);

void timer_settings_show_window(AppState *state);

void timer_settings_dialog_free(gpointer data);
void on_timer_settings_window_destroy(GtkWidget *widget, gpointer user_data);
gboolean on_timer_settings_window_close(GtkWindow *window, gpointer user_data);
void on_timer_settings_changed(GtkSpinButton *spin, gpointer user_data);
void on_app_settings_toggled(GtkCheckButton *button, gpointer user_data);
void on_app_reset_settings_clicked(GtkButton *button, gpointer user_data);
void on_app_archive_all_clicked(GtkButton *button, gpointer user_data);
void on_app_delete_archived_clicked(GtkButton *button, gpointer user_data);
void on_app_delete_stats_clicked(GtkButton *button, gpointer user_data);
void timer_settings_update_controls(TimerSettingsDialog *dialog);
