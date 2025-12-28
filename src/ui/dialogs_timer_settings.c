#include "ui/dialogs.h"
#include "ui/dialogs_timer_settings_internal.h"

#include "core/pomodoro_timer.h"
#include "core/task_store.h"
#include "focus/focus_guard.h"
#include "storage/settings_storage.h"
#include "ui/task_list.h"
#include "utils/autostart.h"

static void timer_settings_update_controls(TimerSettingsDialog *dialog);
static void apply_settings_reset(AppState *state, gpointer user_data);
static void apply_archive_all_tasks(AppState *state, gpointer user_data);
static void apply_delete_archived_tasks(AppState *state, gpointer user_data);
static void apply_delete_stats(AppState *state, gpointer user_data);

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

static void
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

static void
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

static gboolean
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

static void
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
    gtk_widget_set_sensitive(
        GTK_WIDGET(dialog->autostart_start_in_tray_check),
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

  AppState *state = dialog->state;
  AppSettings settings = {
      .close_to_tray = state->close_to_tray,
      .autostart_enabled = state->autostart_enabled,
      .autostart_start_in_tray = state->autostart_start_in_tray,
      .minimize_to_tray = state->minimize_to_tray,
  };

  if (dialog->close_to_tray_check != NULL) {
    settings.close_to_tray =
        gtk_check_button_get_active(dialog->close_to_tray_check);
  }
  if (dialog->autostart_check != NULL) {
    settings.autostart_enabled =
        gtk_check_button_get_active(dialog->autostart_check);
  }
  if (dialog->autostart_start_in_tray_check != NULL) {
    settings.autostart_start_in_tray =
        gtk_check_button_get_active(dialog->autostart_start_in_tray_check);
  }
  if (dialog->minimize_to_tray_check != NULL) {
    settings.minimize_to_tray =
        gtk_check_button_get_active(dialog->minimize_to_tray_check);
  }

  gboolean prev_autostart = state->autostart_enabled;

  state->close_to_tray = settings.close_to_tray;
  state->autostart_enabled = settings.autostart_enabled;
  state->autostart_start_in_tray = settings.autostart_start_in_tray;
  state->minimize_to_tray = settings.minimize_to_tray;

  if (dialog->autostart_start_in_tray_check != NULL) {
    gtk_widget_set_sensitive(
        GTK_WIDGET(dialog->autostart_start_in_tray_check),
        settings.autostart_enabled);
  }

  GError *error = NULL;
  if (!settings_storage_save_app(&settings, &error)) {
    g_warning("Failed to save app settings: %s",
              error ? error->message : "unknown error");
    g_clear_error(&error);
  }

  if (settings.autostart_enabled != prev_autostart) {
    if (!autostart_set_enabled(settings.autostart_enabled, &error)) {
      g_warning("Failed to update autostart settings: %s",
                error ? error->message : "unknown error");
      g_clear_error(&error);
    }
  }
}

static void
on_app_settings_toggled(GtkCheckButton *button, gpointer user_data)
{
  (void)button;
  app_settings_apply(user_data);
}

static void
apply_settings_reset(AppState *state, gpointer user_data)
{
  TimerSettingsDialog *dialog = user_data;
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

static void
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

static void
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

static void
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

static void
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

static void
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

static void
show_timer_settings_window(AppState *state)
{
  if (state == NULL) {
    return;
  }

  if (state->timer_settings_window != NULL) {
    gtk_window_present(state->timer_settings_window);
    return;
  }

  GtkApplication *app = gtk_window_get_application(state->window);
  GtkWidget *window = gtk_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(window), "Settings");
  gtk_window_set_transient_for(GTK_WINDOW(window), state->window);
  gtk_window_set_modal(GTK_WINDOW(window), FALSE);
  gtk_window_set_default_size(GTK_WINDOW(window), 720, 620);

  GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
  gtk_widget_set_margin_top(root, 20);
  gtk_widget_set_margin_bottom(root, 20);
  gtk_widget_set_margin_start(root, 20);
  gtk_widget_set_margin_end(root, 20);
  gtk_widget_add_css_class(root, "settings-root");

  GtkWidget *header = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
  GtkWidget *title = gtk_label_new("Settings");
  gtk_widget_add_css_class(title, "settings-title");
  gtk_widget_set_halign(title, GTK_ALIGN_START);

  GtkWidget *desc = gtk_label_new(
      "Customize your timer and focus guard. Changes apply instantly.");
  gtk_widget_add_css_class(desc, "settings-subtitle");
  gtk_widget_set_halign(desc, GTK_ALIGN_START);
  gtk_label_set_wrap(GTK_LABEL(desc), TRUE);

  gtk_box_append(GTK_BOX(header), title);
  gtk_box_append(GTK_BOX(header), desc);
  gtk_box_append(GTK_BOX(root), header);

  GtkWidget *stack = gtk_stack_new();
  gtk_stack_set_transition_type(GTK_STACK(stack),
                                GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
  gtk_stack_set_transition_duration(GTK_STACK(stack), 180);
  gtk_widget_set_vexpand(stack, TRUE);

  GtkWidget *switcher = gtk_stack_switcher_new();
  gtk_stack_switcher_set_stack(GTK_STACK_SWITCHER(switcher), GTK_STACK(stack));
  gtk_widget_add_css_class(switcher, "settings-switcher");
  gtk_widget_set_halign(switcher, GTK_ALIGN_START);
  gtk_box_append(GTK_BOX(root), switcher);
  gtk_box_append(GTK_BOX(root), stack);

  GtkWidget *timer_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
  gtk_widget_add_css_class(timer_page, "settings-page");
  gtk_widget_set_margin_top(timer_page, 4);
  gtk_widget_set_margin_bottom(timer_page, 8);
  gtk_widget_set_margin_start(timer_page, 2);
  gtk_widget_set_margin_end(timer_page, 2);

  GtkWidget *timer_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_add_css_class(timer_card, "card");

  GtkWidget *timer_title = gtk_label_new("Timer cycle");
  gtk_widget_add_css_class(timer_title, "card-title");
  gtk_widget_set_halign(timer_title, GTK_ALIGN_START);

  GtkWidget *timer_desc =
      gtk_label_new("Adjust the cadence of focus and recovery.");
  gtk_widget_add_css_class(timer_desc, "task-meta");
  gtk_widget_set_halign(timer_desc, GTK_ALIGN_START);
  gtk_label_set_wrap(GTK_LABEL(timer_desc), TRUE);

  GtkWidget *timer_grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(timer_grid), 10);
  gtk_grid_set_column_spacing(GTK_GRID(timer_grid), 16);

  GtkWidget *focus_label = gtk_label_new("Focus minutes");
  gtk_widget_add_css_class(focus_label, "setting-label");
  gtk_widget_set_halign(focus_label, GTK_ALIGN_START);
  gtk_widget_set_hexpand(focus_label, TRUE);
  GtkWidget *focus_spin = gtk_spin_button_new_with_range(1, 120, 1);
  gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(focus_spin), TRUE);
  gtk_widget_add_css_class(focus_spin, "setting-spin");
  gtk_widget_set_halign(focus_spin, GTK_ALIGN_END);

  GtkWidget *short_label = gtk_label_new("Short break");
  gtk_widget_add_css_class(short_label, "setting-label");
  gtk_widget_set_halign(short_label, GTK_ALIGN_START);
  gtk_widget_set_hexpand(short_label, TRUE);
  GtkWidget *short_spin = gtk_spin_button_new_with_range(1, 30, 1);
  gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(short_spin), TRUE);
  gtk_widget_add_css_class(short_spin, "setting-spin");
  gtk_widget_set_halign(short_spin, GTK_ALIGN_END);

  GtkWidget *long_label = gtk_label_new("Long break");
  gtk_widget_add_css_class(long_label, "setting-label");
  gtk_widget_set_halign(long_label, GTK_ALIGN_START);
  gtk_widget_set_hexpand(long_label, TRUE);
  GtkWidget *long_spin = gtk_spin_button_new_with_range(1, 60, 1);
  gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(long_spin), TRUE);
  gtk_widget_add_css_class(long_spin, "setting-spin");
  gtk_widget_set_halign(long_spin, GTK_ALIGN_END);

  GtkWidget *interval_label = gtk_label_new("Long break every (sessions)");
  gtk_widget_add_css_class(interval_label, "setting-label");
  gtk_widget_set_halign(interval_label, GTK_ALIGN_START);
  gtk_widget_set_hexpand(interval_label, TRUE);
  GtkWidget *interval_spin = gtk_spin_button_new_with_range(1, 12, 1);
  gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(interval_spin), TRUE);
  gtk_widget_add_css_class(interval_spin, "setting-spin");
  gtk_widget_set_halign(interval_spin, GTK_ALIGN_END);

  gtk_grid_attach(GTK_GRID(timer_grid), focus_label, 0, 0, 1, 1);
  gtk_grid_attach(GTK_GRID(timer_grid), focus_spin, 1, 0, 1, 1);
  gtk_grid_attach(GTK_GRID(timer_grid), short_label, 0, 1, 1, 1);
  gtk_grid_attach(GTK_GRID(timer_grid), short_spin, 1, 1, 1, 1);
  gtk_grid_attach(GTK_GRID(timer_grid), long_label, 0, 2, 1, 1);
  gtk_grid_attach(GTK_GRID(timer_grid), long_spin, 1, 2, 1, 1);
  gtk_grid_attach(GTK_GRID(timer_grid), interval_label, 0, 3, 1, 1);
  gtk_grid_attach(GTK_GRID(timer_grid), interval_spin, 1, 3, 1, 1);

  gtk_box_append(GTK_BOX(timer_card), timer_title);
  gtk_box_append(GTK_BOX(timer_card), timer_desc);
  gtk_box_append(GTK_BOX(timer_card), timer_grid);

  gtk_box_append(GTK_BOX(timer_page), timer_card);

  GtkWidget *timer_scroller = gtk_scrolled_window_new();
  gtk_widget_add_css_class(timer_scroller, "settings-scroller");
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(timer_scroller),
                                 GTK_POLICY_NEVER,
                                 GTK_POLICY_AUTOMATIC);
  gtk_widget_set_vexpand(timer_scroller, TRUE);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(timer_scroller), timer_page);

  GtkWidget *app_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
  gtk_widget_add_css_class(app_page, "settings-page");
  gtk_widget_set_margin_top(app_page, 4);
  gtk_widget_set_margin_bottom(app_page, 8);
  gtk_widget_set_margin_start(app_page, 2);
  gtk_widget_set_margin_end(app_page, 2);

  GtkWidget *app_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_add_css_class(app_card, "card");

  GtkWidget *app_title = gtk_label_new("Startup & tray");
  gtk_widget_add_css_class(app_title, "card-title");
  gtk_widget_set_halign(app_title, GTK_ALIGN_START);

  GtkWidget *app_desc =
      gtk_label_new("Configure how the app launches and hides.");
  gtk_widget_add_css_class(app_desc, "task-meta");
  gtk_widget_set_halign(app_desc, GTK_ALIGN_START);
  gtk_label_set_wrap(GTK_LABEL(app_desc), TRUE);

  GtkWidget *app_grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(app_grid), 10);
  gtk_grid_set_column_spacing(GTK_GRID(app_grid), 16);

  GtkWidget *autostart_label = gtk_label_new("Autostart on login");
  gtk_widget_add_css_class(autostart_label, "setting-label");
  gtk_widget_set_halign(autostart_label, GTK_ALIGN_START);
  gtk_widget_set_hexpand(autostart_label, TRUE);
  GtkWidget *autostart_check = gtk_check_button_new();
  gtk_widget_set_halign(autostart_check, GTK_ALIGN_END);

  GtkWidget *autostart_tray_label =
      gtk_label_new("Start in tray when autostarting");
  gtk_widget_add_css_class(autostart_tray_label, "setting-label");
  gtk_widget_set_halign(autostart_tray_label, GTK_ALIGN_START);
  gtk_widget_set_hexpand(autostart_tray_label, TRUE);
  gtk_label_set_wrap(GTK_LABEL(autostart_tray_label), TRUE);
  GtkWidget *autostart_tray_check = gtk_check_button_new();
  gtk_widget_set_halign(autostart_tray_check, GTK_ALIGN_END);

  GtkWidget *minimize_label = gtk_label_new("Minimize to tray");
  gtk_widget_add_css_class(minimize_label, "setting-label");
  gtk_widget_set_halign(minimize_label, GTK_ALIGN_START);
  gtk_widget_set_hexpand(minimize_label, TRUE);
  GtkWidget *minimize_check = gtk_check_button_new();
  gtk_widget_set_halign(minimize_check, GTK_ALIGN_END);

  GtkWidget *tray_label = gtk_label_new("Close to tray");
  gtk_widget_add_css_class(tray_label, "setting-label");
  gtk_widget_set_halign(tray_label, GTK_ALIGN_START);
  gtk_widget_set_hexpand(tray_label, TRUE);
  GtkWidget *tray_check = gtk_check_button_new();
  gtk_widget_set_halign(tray_check, GTK_ALIGN_END);

  gtk_grid_attach(GTK_GRID(app_grid), autostart_label, 0, 0, 1, 1);
  gtk_grid_attach(GTK_GRID(app_grid), autostart_check, 1, 0, 1, 1);
  gtk_grid_attach(GTK_GRID(app_grid), autostart_tray_label, 0, 1, 1, 1);
  gtk_grid_attach(GTK_GRID(app_grid), autostart_tray_check, 1, 1, 1, 1);
  gtk_grid_attach(GTK_GRID(app_grid), minimize_label, 0, 2, 1, 1);
  gtk_grid_attach(GTK_GRID(app_grid), minimize_check, 1, 2, 1, 1);
  gtk_grid_attach(GTK_GRID(app_grid), tray_label, 0, 3, 1, 1);
  gtk_grid_attach(GTK_GRID(app_grid), tray_check, 1, 3, 1, 1);

  gtk_box_append(GTK_BOX(app_card), app_title);
  gtk_box_append(GTK_BOX(app_card), app_desc);
  gtk_box_append(GTK_BOX(app_card), app_grid);

  gtk_box_append(GTK_BOX(app_page), app_card);

  GtkWidget *data_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_add_css_class(data_card, "card");

  GtkWidget *data_title = gtk_label_new("Data & maintenance");
  gtk_widget_add_css_class(data_title, "card-title");
  gtk_widget_set_halign(data_title, GTK_ALIGN_START);

  GtkWidget *data_desc = gtk_label_new(
      "Run bulk actions on settings, tasks, or usage stats.");
  gtk_widget_add_css_class(data_desc, "task-meta");
  gtk_widget_set_halign(data_desc, GTK_ALIGN_START);
  gtk_label_set_wrap(GTK_LABEL(data_desc), TRUE);

  GtkWidget *data_grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(data_grid), 10);
  gtk_grid_set_column_spacing(GTK_GRID(data_grid), 16);

  GtkWidget *reset_label = gtk_label_new("Reset settings to defaults");
  gtk_widget_add_css_class(reset_label, "setting-label");
  gtk_widget_set_halign(reset_label, GTK_ALIGN_START);
  gtk_widget_set_hexpand(reset_label, TRUE);
  GtkWidget *reset_button = gtk_button_new_with_label("Reset");
  gtk_widget_add_css_class(reset_button, "btn-secondary");
  gtk_widget_add_css_class(reset_button, "btn-compact");
  gtk_widget_set_halign(reset_button, GTK_ALIGN_END);

  GtkWidget *archive_all_label = gtk_label_new("Archive all tasks");
  gtk_widget_add_css_class(archive_all_label, "setting-label");
  gtk_widget_set_halign(archive_all_label, GTK_ALIGN_START);
  gtk_widget_set_hexpand(archive_all_label, TRUE);
  GtkWidget *archive_all_button = gtk_button_new_with_label("Archive all");
  gtk_widget_add_css_class(archive_all_button, "btn-secondary");
  gtk_widget_add_css_class(archive_all_button, "btn-compact");
  gtk_widget_set_halign(archive_all_button, GTK_ALIGN_END);

  GtkWidget *delete_archived_label = gtk_label_new("Delete archived tasks");
  gtk_widget_add_css_class(delete_archived_label, "setting-label");
  gtk_widget_set_halign(delete_archived_label, GTK_ALIGN_START);
  gtk_widget_set_hexpand(delete_archived_label, TRUE);
  GtkWidget *delete_archived_button =
      gtk_button_new_with_label("Delete archived");
  gtk_widget_add_css_class(delete_archived_button, "btn-danger");
  gtk_widget_add_css_class(delete_archived_button, "btn-compact");
  gtk_widget_set_halign(delete_archived_button, GTK_ALIGN_END);

  GtkWidget *delete_stats_label = gtk_label_new("Delete all usage stats");
  gtk_widget_add_css_class(delete_stats_label, "setting-label");
  gtk_widget_set_halign(delete_stats_label, GTK_ALIGN_START);
  gtk_widget_set_hexpand(delete_stats_label, TRUE);
  GtkWidget *delete_stats_button = gtk_button_new_with_label("Delete stats");
  gtk_widget_add_css_class(delete_stats_button, "btn-danger");
  gtk_widget_add_css_class(delete_stats_button, "btn-compact");
  gtk_widget_set_halign(delete_stats_button, GTK_ALIGN_END);

  gtk_grid_attach(GTK_GRID(data_grid), reset_label, 0, 0, 1, 1);
  gtk_grid_attach(GTK_GRID(data_grid), reset_button, 1, 0, 1, 1);
  gtk_grid_attach(GTK_GRID(data_grid), archive_all_label, 0, 1, 1, 1);
  gtk_grid_attach(GTK_GRID(data_grid), archive_all_button, 1, 1, 1, 1);
  gtk_grid_attach(GTK_GRID(data_grid), delete_archived_label, 0, 2, 1, 1);
  gtk_grid_attach(GTK_GRID(data_grid), delete_archived_button, 1, 2, 1, 1);
  gtk_grid_attach(GTK_GRID(data_grid), delete_stats_label, 0, 3, 1, 1);
  gtk_grid_attach(GTK_GRID(data_grid), delete_stats_button, 1, 3, 1, 1);

  gtk_box_append(GTK_BOX(data_card), data_title);
  gtk_box_append(GTK_BOX(data_card), data_desc);
  gtk_box_append(GTK_BOX(data_card), data_grid);

  gtk_box_append(GTK_BOX(app_page), data_card);

  GtkWidget *app_scroller = gtk_scrolled_window_new();
  gtk_widget_add_css_class(app_scroller, "settings-scroller");
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(app_scroller),
                                 GTK_POLICY_NEVER,
                                 GTK_POLICY_AUTOMATIC);
  gtk_widget_set_vexpand(app_scroller, TRUE);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(app_scroller), app_page);

  GtkWidget *focus_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
  gtk_widget_add_css_class(focus_page, "settings-page");
  gtk_widget_set_margin_top(focus_page, 4);
  gtk_widget_set_margin_bottom(focus_page, 8);
  gtk_widget_set_margin_start(focus_page, 2);
  gtk_widget_set_margin_end(focus_page, 2);

  GtkWidget *focus_scroller = gtk_scrolled_window_new();
  gtk_widget_add_css_class(focus_scroller, "settings-scroller");
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(focus_scroller),
                                 GTK_POLICY_NEVER,
                                 GTK_POLICY_AUTOMATIC);
  gtk_widget_set_vexpand(focus_scroller, TRUE);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(focus_scroller), focus_page);

  gboolean ollama_available = state->focus_guard != NULL &&
                              focus_guard_is_ollama_available(state->focus_guard);
  GtkWidget *chrome_page = NULL;
  GtkWidget *chrome_scroller = NULL;
  if (ollama_available) {
    chrome_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_add_css_class(chrome_page, "settings-page");
    gtk_widget_set_margin_top(chrome_page, 4);
    gtk_widget_set_margin_bottom(chrome_page, 8);
    gtk_widget_set_margin_start(chrome_page, 2);
    gtk_widget_set_margin_end(chrome_page, 2);

    chrome_scroller = gtk_scrolled_window_new();
    gtk_widget_add_css_class(chrome_scroller, "settings-scroller");
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(chrome_scroller),
                                   GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(chrome_scroller, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(chrome_scroller),
                                  chrome_page);
  }

  TimerSettingsDialog *dialog = g_new0(TimerSettingsDialog, 1);
  dialog->state = state;
  dialog->window = GTK_WINDOW(window);
  dialog->focus_guard_model = focus_guard_settings_model_new();
  dialog->focus_spin = GTK_SPIN_BUTTON(focus_spin);
  dialog->short_spin = GTK_SPIN_BUTTON(short_spin);
  dialog->long_spin = GTK_SPIN_BUTTON(long_spin);
  dialog->interval_spin = GTK_SPIN_BUTTON(interval_spin);
  dialog->close_to_tray_check = GTK_CHECK_BUTTON(tray_check);
  dialog->autostart_check = GTK_CHECK_BUTTON(autostart_check);
  dialog->autostart_start_in_tray_check =
      GTK_CHECK_BUTTON(autostart_tray_check);
  dialog->minimize_to_tray_check = GTK_CHECK_BUTTON(minimize_check);

  g_signal_connect(focus_spin,
                   "value-changed",
                   G_CALLBACK(on_timer_settings_changed),
                   dialog);
  g_signal_connect(short_spin,
                   "value-changed",
                   G_CALLBACK(on_timer_settings_changed),
                   dialog);
  g_signal_connect(long_spin,
                   "value-changed",
                   G_CALLBACK(on_timer_settings_changed),
                   dialog);
  g_signal_connect(interval_spin,
                   "value-changed",
                   G_CALLBACK(on_timer_settings_changed),
                   dialog);
  g_signal_connect(tray_check,
                   "toggled",
                   G_CALLBACK(on_app_settings_toggled),
                   dialog);
  g_signal_connect(autostart_check,
                   "toggled",
                   G_CALLBACK(on_app_settings_toggled),
                   dialog);
  g_signal_connect(autostart_tray_check,
                   "toggled",
                   G_CALLBACK(on_app_settings_toggled),
                   dialog);
  g_signal_connect(minimize_check,
                   "toggled",
                   G_CALLBACK(on_app_settings_toggled),
                   dialog);
  g_signal_connect(reset_button,
                   "clicked",
                   G_CALLBACK(on_app_reset_settings_clicked),
                   dialog);
  g_signal_connect(archive_all_button,
                   "clicked",
                   G_CALLBACK(on_app_archive_all_clicked),
                   dialog);
  g_signal_connect(delete_archived_button,
                   "clicked",
                   G_CALLBACK(on_app_delete_archived_clicked),
                   dialog);
  g_signal_connect(delete_stats_button,
                   "clicked",
                   G_CALLBACK(on_app_delete_stats_clicked),
                   dialog);

  focus_guard_settings_append(dialog, focus_page, chrome_page);

  gtk_stack_add_titled(GTK_STACK(stack), timer_scroller, "timer", "Timer");
  gtk_stack_add_titled(GTK_STACK(stack), app_scroller, "app", "App");
  gtk_stack_add_titled(GTK_STACK(stack), focus_scroller, "focus", "Focus guard");
  if (chrome_scroller != NULL && dialog->focus_guard_ollama_section != NULL) {
    gtk_stack_add_titled(GTK_STACK(stack), chrome_scroller, "chrome", "Chrome");
  }

  gtk_window_set_child(GTK_WINDOW(window), root);
  state->timer_settings_window = GTK_WINDOW(window);

  g_object_set_data_full(G_OBJECT(window),
                         "timer-settings-dialog",
                         dialog,
                         timer_settings_dialog_free);

  g_signal_connect(window,
                   "close-request",
                   G_CALLBACK(on_timer_settings_window_close),
                   dialog);
  g_signal_connect(window,
                   "destroy",
                   G_CALLBACK(on_timer_settings_window_destroy),
                   dialog);

  timer_settings_update_controls(dialog);
  focus_guard_start_active_monitor(dialog);
  gtk_window_present(GTK_WINDOW(window));
}

void
dialogs_on_show_timer_settings_clicked(GtkButton *button, gpointer user_data)
{
  (void)button;
  show_timer_settings_window((AppState *)user_data);
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
