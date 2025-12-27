#include "ui/dialogs.h"
#include "ui/dialogs_timer_settings_internal.h"

#include "core/pomodoro_timer.h"
#include "storage/settings_storage.h"

static void timer_settings_update_controls(TimerSettingsDialog *dialog);

static void
timer_settings_dialog_free(gpointer data)
{
  TimerSettingsDialog *dialog = data;
  if (dialog == NULL) {
    return;
  }

  if (dialog->focus_guard_active_source != 0) {
    g_source_remove(dialog->focus_guard_active_source);
    dialog->focus_guard_active_source = 0;
  }

  g_free(dialog->focus_guard_last_external);
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

  dialog->suppress_signals = TRUE;
  g_info("Timer settings window destroyed");
  if (dialog->state != NULL) {
    dialog->state->timer_settings_window = NULL;
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
on_close_to_tray_toggled(GtkCheckButton *button, gpointer user_data)
{
  TimerSettingsDialog *dialog = user_data;
  if (dialog == NULL || dialog->suppress_signals || dialog->state == NULL) {
    return;
  }

  AppSettings settings = {.close_to_tray = TRUE};
  if (button != NULL) {
    settings.close_to_tray = gtk_check_button_get_active(button);
  }

  dialog->state->close_to_tray = settings.close_to_tray;

  GError *error = NULL;
  if (!settings_storage_save_app(&settings, &error)) {
    g_warning("Failed to save app settings: %s",
              error ? error->message : "unknown error");
    g_clear_error(&error);
  }
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

  if (dialog->close_to_tray_check != NULL) {
    gtk_check_button_set_active(dialog->close_to_tray_check,
                                dialog->state->close_to_tray);
  }

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
  gtk_window_set_title(GTK_WINDOW(window), "Timer Settings");
  gtk_window_set_transient_for(GTK_WINDOW(window), state->window);
  gtk_window_set_modal(GTK_WINDOW(window), TRUE);
  gtk_window_set_default_size(GTK_WINDOW(window), 460, 560);

  GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_top(root, 18);
  gtk_widget_set_margin_bottom(root, 18);
  gtk_widget_set_margin_start(root, 18);
  gtk_widget_set_margin_end(root, 18);

  GtkWidget *title = gtk_label_new("Pomodoro timer");
  gtk_widget_add_css_class(title, "card-title");
  gtk_widget_set_halign(title, GTK_ALIGN_START);

  GtkWidget *desc =
      gtk_label_new("Tune your focus and break durations. Updates apply instantly.");
  gtk_widget_add_css_class(desc, "task-meta");
  gtk_widget_set_halign(desc, GTK_ALIGN_START);
  gtk_label_set_wrap(GTK_LABEL(desc), TRUE);

  GtkWidget *focus_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  GtkWidget *focus_label = gtk_label_new("Focus minutes");
  gtk_widget_add_css_class(focus_label, "setting-label");
  gtk_widget_set_halign(focus_label, GTK_ALIGN_START);
  gtk_widget_set_hexpand(focus_label, TRUE);
  GtkWidget *focus_spin = gtk_spin_button_new_with_range(1, 120, 1);
  gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(focus_spin), TRUE);
  gtk_widget_add_css_class(focus_spin, "setting-spin");
  gtk_widget_set_halign(focus_spin, GTK_ALIGN_END);

  gtk_box_append(GTK_BOX(focus_row), focus_label);
  gtk_box_append(GTK_BOX(focus_row), focus_spin);

  GtkWidget *short_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  GtkWidget *short_label = gtk_label_new("Short break");
  gtk_widget_add_css_class(short_label, "setting-label");
  gtk_widget_set_halign(short_label, GTK_ALIGN_START);
  gtk_widget_set_hexpand(short_label, TRUE);
  GtkWidget *short_spin = gtk_spin_button_new_with_range(1, 30, 1);
  gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(short_spin), TRUE);
  gtk_widget_add_css_class(short_spin, "setting-spin");
  gtk_widget_set_halign(short_spin, GTK_ALIGN_END);

  gtk_box_append(GTK_BOX(short_row), short_label);
  gtk_box_append(GTK_BOX(short_row), short_spin);

  GtkWidget *long_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  GtkWidget *long_label = gtk_label_new("Long break");
  gtk_widget_add_css_class(long_label, "setting-label");
  gtk_widget_set_halign(long_label, GTK_ALIGN_START);
  gtk_widget_set_hexpand(long_label, TRUE);
  GtkWidget *long_spin = gtk_spin_button_new_with_range(1, 60, 1);
  gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(long_spin), TRUE);
  gtk_widget_add_css_class(long_spin, "setting-spin");
  gtk_widget_set_halign(long_spin, GTK_ALIGN_END);

  gtk_box_append(GTK_BOX(long_row), long_label);
  gtk_box_append(GTK_BOX(long_row), long_spin);

  GtkWidget *interval_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  GtkWidget *interval_label = gtk_label_new("Long break every (sessions)");
  gtk_widget_add_css_class(interval_label, "setting-label");
  gtk_widget_set_halign(interval_label, GTK_ALIGN_START);
  gtk_widget_set_hexpand(interval_label, TRUE);
  GtkWidget *interval_spin = gtk_spin_button_new_with_range(1, 12, 1);
  gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(interval_spin), TRUE);
  gtk_widget_add_css_class(interval_spin, "setting-spin");
  gtk_widget_set_halign(interval_spin, GTK_ALIGN_END);

  gtk_box_append(GTK_BOX(interval_row), interval_label);
  gtk_box_append(GTK_BOX(interval_row), interval_spin);

  GtkWidget *tray_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  GtkWidget *tray_label = gtk_label_new("Close to tray");
  gtk_widget_add_css_class(tray_label, "setting-label");
  gtk_widget_set_halign(tray_label, GTK_ALIGN_START);
  gtk_widget_set_hexpand(tray_label, TRUE);
  GtkWidget *tray_check = gtk_check_button_new();
  gtk_widget_set_halign(tray_check, GTK_ALIGN_END);

  gtk_box_append(GTK_BOX(tray_row), tray_label);
  gtk_box_append(GTK_BOX(tray_row), tray_check);

  gtk_box_append(GTK_BOX(root), title);
  gtk_box_append(GTK_BOX(root), desc);
  gtk_box_append(GTK_BOX(root), focus_row);
  gtk_box_append(GTK_BOX(root), short_row);
  gtk_box_append(GTK_BOX(root), long_row);
  gtk_box_append(GTK_BOX(root), interval_row);
  gtk_box_append(GTK_BOX(root), tray_row);

  TimerSettingsDialog *dialog = g_new0(TimerSettingsDialog, 1);
  dialog->state = state;
  dialog->window = GTK_WINDOW(window);
  dialog->focus_spin = GTK_SPIN_BUTTON(focus_spin);
  dialog->short_spin = GTK_SPIN_BUTTON(short_spin);
  dialog->long_spin = GTK_SPIN_BUTTON(long_spin);
  dialog->interval_spin = GTK_SPIN_BUTTON(interval_spin);
  dialog->close_to_tray_check = GTK_CHECK_BUTTON(tray_check);

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
                   G_CALLBACK(on_close_to_tray_toggled),
                   dialog);

  focus_guard_settings_append(dialog, root);

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
