#include "ui/dialogs_timer_settings_internal.h"

#include "core/pomodoro_timer.h"
#include "core/task_store.h"
#include "focus/focus_guard.h"
#include "storage/settings_storage.h"
#include "ui/dialogs.h"
#include "ui/task_list.h"
#include "utils/autostart.h"

static void
timer_settings_dialog_teardown(TimerSettingsDialog *dialog)
{
  if (dialog == NULL) {
    return;
  }

  dialog->suppress_signals = TRUE;

  if (dialog->focus_guard_active_source != 0) {
    g_source_remove(dialog->focus_guard_active_source);
    dialog->focus_guard_active_source = 0;
  }

  if (dialog->focus_guard_model != NULL) {
    focus_guard_settings_model_cancel_refresh(dialog->focus_guard_model);
  }

  dialog->focus_guard_ollama_dropdown = NULL;
  dialog->focus_guard_ollama_refresh_button = NULL;
  dialog->focus_guard_ollama_status_label = NULL;
  dialog->focus_guard_trafilatura_status_label = NULL;
  dialog->focus_guard_chrome_check = NULL;
  dialog->focus_guard_chrome_port_spin = NULL;
  dialog->focus_guard_list = NULL;
  dialog->focus_guard_empty_label = NULL;
  dialog->focus_guard_entry = NULL;
  dialog->focus_guard_active_label = NULL;
  dialog->close_to_tray_check = NULL;
  dialog->autostart_check = NULL;
  dialog->autostart_start_in_tray_check = NULL;
  dialog->minimize_to_tray_check = NULL;

  dialog->window = NULL;
  dialog->state = NULL;
}

void
timer_settings_dialog_free(gpointer data)
{
  TimerSettingsDialog *dialog = data;
  if (dialog == NULL) {
    return;
  }

  timer_settings_dialog_teardown(dialog);
  g_clear_object(&dialog->focus_guard_model);
  g_free(dialog);
}

void
on_timer_settings_window_destroy(GtkWidget *widget, gpointer user_data)
{
  (void)widget;
  TimerSettingsDialog *dialog = user_data;
  if (dialog == NULL) {
    return;
  }

  AppState *state = dialog->state;
  timer_settings_dialog_teardown(dialog);
  g_info("Timer settings window destroyed");
  if (state != NULL) {
    state->timer_settings_window = NULL;
  }
}

gboolean
on_timer_settings_window_close(GtkWindow *window, gpointer user_data)
{
  (void)window;
  TimerSettingsDialog *dialog = user_data;
  if (dialog != NULL) {
    dialog->suppress_signals = TRUE;
    g_info("Timer settings window close requested");
  }
  return FALSE;
}

void
on_timer_settings_changed(GtkSpinButton *spin, gpointer user_data)
{
  (void)spin;

  TimerSettingsDialog *dialog = user_data;
  if (dialog == NULL || dialog->suppress_signals || dialog->state == NULL ||
      dialog->state->timer == NULL) {
    return;
  }

  PomodoroTimerConfig config =
      pomodoro_timer_get_config(dialog->state->timer);

  if (dialog->focus_spin != NULL) {
    config.focus_minutes =
        (guint)gtk_spin_button_get_value_as_int(dialog->focus_spin);
  }
  if (dialog->short_spin != NULL) {
    config.short_break_minutes =
        (guint)gtk_spin_button_get_value_as_int(dialog->short_spin);
  }
  if (dialog->long_spin != NULL) {
    config.long_break_minutes =
        (guint)gtk_spin_button_get_value_as_int(dialog->long_spin);
  }
  if (dialog->interval_spin != NULL) {
    config.long_break_interval =
        (guint)gtk_spin_button_get_value_as_int(dialog->interval_spin);
  }

  config = pomodoro_timer_config_normalize(config);
  pomodoro_timer_apply_config(dialog->state->timer, config);

  GError *error = NULL;
  if (!settings_storage_save_timer(&config, &error)) {
    g_warning("Failed to save timer settings: %s",
              error ? error->message : "unknown error");
    g_clear_error(&error);
  }
}

static void
app_settings_update_controls(TimerSettingsDialog *dialog)
{
  if (dialog == NULL || dialog->state == NULL) {
    return;
  }

  if (dialog->close_to_tray_check != NULL) {
    gtk_check_button_set_active(dialog->close_to_tray_check,
                                dialog->state->close_to_tray);
  }

  if (dialog->autostart_check != NULL) {
    gtk_check_button_set_active(dialog->autostart_check,
                                dialog->state->autostart_enabled);
  }

  if (dialog->autostart_start_in_tray_check != NULL) {
    gtk_check_button_set_active(dialog->autostart_start_in_tray_check,
                                dialog->state->autostart_start_in_tray);
    gtk_widget_set_sensitive(GTK_WIDGET(dialog->autostart_start_in_tray_check),
                             dialog->state->autostart_enabled);
  }

  if (dialog->minimize_to_tray_check != NULL) {
    gtk_check_button_set_active(dialog->minimize_to_tray_check,
                                dialog->state->minimize_to_tray);
  }
}

static void
app_settings_apply(TimerSettingsDialog *dialog)
{
  if (dialog == NULL || dialog->suppress_signals || dialog->state == NULL) {
    return;
  }

  AppSettings app_settings = settings_storage_app_default();
  app_settings.close_to_tray = dialog->state->close_to_tray;
  app_settings.autostart_enabled = dialog->state->autostart_enabled;
  app_settings.autostart_start_in_tray = dialog->state->autostart_start_in_tray;
  app_settings.minimize_to_tray = dialog->state->minimize_to_tray;

  if (dialog->close_to_tray_check != NULL) {
    app_settings.close_to_tray =
        gtk_check_button_get_active(dialog->close_to_tray_check);
  }
  if (dialog->autostart_check != NULL) {
    app_settings.autostart_enabled =
        gtk_check_button_get_active(dialog->autostart_check);
  }
  if (dialog->autostart_start_in_tray_check != NULL) {
    app_settings.autostart_start_in_tray =
        gtk_check_button_get_active(dialog->autostart_start_in_tray_check);
  }
  if (dialog->minimize_to_tray_check != NULL) {
    app_settings.minimize_to_tray =
        gtk_check_button_get_active(dialog->minimize_to_tray_check);
  }

  gboolean prev_autostart = dialog->state->autostart_enabled;
  dialog->state->close_to_tray = app_settings.close_to_tray;
  dialog->state->autostart_enabled = app_settings.autostart_enabled;
  dialog->state->autostart_start_in_tray = app_settings.autostart_start_in_tray;
  dialog->state->minimize_to_tray = app_settings.minimize_to_tray;

  GError *error = NULL;
  if (!settings_storage_save_app(&app_settings, &error)) {
    g_warning("Failed to save app settings: %s",
              error ? error->message : "unknown error");
    g_clear_error(&error);
  }

  if (app_settings.autostart_enabled != prev_autostart) {
    if (!autostart_set_enabled(app_settings.autostart_enabled, &error)) {
      g_warning("Failed to update autostart settings: %s",
                error ? error->message : "unknown error");
      g_clear_error(&error);
    }
  }

  app_settings_update_controls(dialog);
}

void
on_app_settings_toggled(GtkCheckButton *button, gpointer user_data)
{
  (void)button;
  app_settings_apply(user_data);
}

static void
apply_settings_reset(AppState *state, gpointer user_data)
{
  if (state == NULL) {
    return;
  }

  PomodoroTimerConfig config = pomodoro_timer_config_default();
  if (state->timer != NULL) {
    pomodoro_timer_apply_config(state->timer, config);
  }

  GError *error = NULL;
  if (!settings_storage_save_timer(&config, &error)) {
    g_warning("Failed to save timer defaults: %s",
              error ? error->message : "unknown error");
    g_clear_error(&error);
  }

  AppSettings app_settings = settings_storage_app_default();
  gboolean prev_autostart = state->autostart_enabled;

  state->close_to_tray = app_settings.close_to_tray;
  state->autostart_enabled = app_settings.autostart_enabled;
  state->autostart_start_in_tray = app_settings.autostart_start_in_tray;
  state->minimize_to_tray = app_settings.minimize_to_tray;

  if (!settings_storage_save_app(&app_settings, &error)) {
    g_warning("Failed to save app defaults: %s",
              error ? error->message : "unknown error");
    g_clear_error(&error);
  }

  if (app_settings.autostart_enabled != prev_autostart) {
    if (!autostart_set_enabled(app_settings.autostart_enabled, &error)) {
      g_warning("Failed to update autostart defaults: %s",
                error ? error->message : "unknown error");
      g_clear_error(&error);
    }
  }

  FocusGuardConfig guard_config = focus_guard_config_default();
  if (state->focus_guard != NULL) {
    focus_guard_apply_config(state->focus_guard, guard_config);
  }
  if (!settings_storage_save_focus_guard(&guard_config, &error)) {
    g_warning("Failed to save focus guard defaults: %s",
              error ? error->message : "unknown error");
    g_clear_error(&error);
  }
  focus_guard_config_clear(&guard_config);

  TimerSettingsDialog *dialog = user_data;
  if (dialog != NULL) {
    timer_settings_update_controls(dialog);
  }
}

static void
apply_archive_all_tasks(AppState *state, gpointer user_data)
{
  (void)user_data;
  if (state == NULL || state->store == NULL) {
    return;
  }

  task_store_archive_all(state->store);
  task_list_save_store(state);
  task_list_refresh(state);
}

static void
apply_delete_archived_tasks(AppState *state, gpointer user_data)
{
  (void)user_data;
  if (state == NULL || state->store == NULL) {
    return;
  }

  if (task_store_remove_archived(state->store) == 0) {
    return;
  }
  task_list_save_store(state);
  task_list_refresh(state);
}

static void
apply_delete_stats(AppState *state, gpointer user_data)
{
  (void)user_data;
  if (state == NULL || state->focus_guard == NULL) {
    return;
  }

  focus_guard_clear_stats(state->focus_guard);
}

void
on_app_reset_settings_clicked(GtkButton *button, gpointer user_data)
{
  (void)button;
  TimerSettingsDialog *dialog = user_data;
  if (dialog == NULL || dialog->state == NULL) {
    return;
  }

  dialogs_show_confirm_action(
      dialog->state,
      "Reset settings?",
      "This will restore timer, app, focus guard, and Chrome settings to their defaults.",
      apply_settings_reset,
      dialog,
      NULL);
}

void
on_app_archive_all_clicked(GtkButton *button, gpointer user_data)
{
  (void)button;
  TimerSettingsDialog *dialog = user_data;
  if (dialog == NULL || dialog->state == NULL) {
    return;
  }

  dialogs_show_confirm_action(
      dialog->state,
      "Archive all tasks?",
      "All active, pending, and completed tasks will move to the archive. You can restore them later.",
      apply_archive_all_tasks,
      NULL,
      NULL);
}

void
on_app_delete_archived_clicked(GtkButton *button, gpointer user_data)
{
  (void)button;
  TimerSettingsDialog *dialog = user_data;
  if (dialog == NULL || dialog->state == NULL) {
    return;
  }

  dialogs_show_confirm_action(
      dialog->state,
      "Delete archived tasks?",
      "Archived tasks will be permanently removed.",
      apply_delete_archived_tasks,
      NULL,
      NULL);
}

void
on_app_delete_stats_clicked(GtkButton *button, gpointer user_data)
{
  (void)button;
  TimerSettingsDialog *dialog = user_data;
  if (dialog == NULL || dialog->state == NULL) {
    return;
  }

  dialogs_show_confirm_action(
      dialog->state,
      "Delete all usage stats?",
      "Stored focus guard usage stats will be permanently removed.",
      apply_delete_stats,
      NULL,
      NULL);
}

void
timer_settings_update_controls(TimerSettingsDialog *dialog)
{
  if (dialog == NULL || dialog->state == NULL || dialog->state->timer == NULL) {
    return;
  }

  gboolean prev_suppress = dialog->suppress_signals;
  dialog->suppress_signals = TRUE;

  PomodoroTimerConfig config =
      pomodoro_timer_get_config(dialog->state->timer);

  if (dialog->focus_spin != NULL) {
    gtk_spin_button_set_value(dialog->focus_spin, (gdouble)config.focus_minutes);
  }
  if (dialog->short_spin != NULL) {
    gtk_spin_button_set_value(dialog->short_spin,
                              (gdouble)config.short_break_minutes);
  }
  if (dialog->long_spin != NULL) {
    gtk_spin_button_set_value(dialog->long_spin,
                              (gdouble)config.long_break_minutes);
  }
  if (dialog->interval_spin != NULL) {
    gtk_spin_button_set_value(dialog->interval_spin,
                              (gdouble)config.long_break_interval);
  }

  app_settings_update_controls(dialog);
  focus_guard_settings_update_controls(dialog);

  dialog->suppress_signals = prev_suppress;
}

void
dialogs_on_show_timer_settings_clicked(GtkButton *button, gpointer user_data)
{
  (void)button;
  timer_settings_show_window((AppState *)user_data);
}

void
dialogs_cleanup_timer_settings(AppState *state)
{
  if (state == NULL || state->timer_settings_window == NULL) {
    return;
  }

  TimerSettingsDialog *dialog = g_object_get_data(
      G_OBJECT(state->timer_settings_window),
      "timer-settings-dialog");
  if (dialog != NULL) {
    dialog->state = NULL;
  }

  gtk_window_destroy(state->timer_settings_window);
  state->timer_settings_window = NULL;
}
