#include "ui/dialogs.h"

#include "core/pomodoro_timer.h"
#include "focus/focus_guard.h"
#include "focus/focus_guard_x11.h"
#include "storage/settings_storage.h"
#include "ui/task_list.h"

typedef struct {
  AppState *state;
  PomodoroTask *task;
  GtkWidget *window;
  DialogConfirmAction action;
} ConfirmDialog;

typedef struct {
  AppState *state;
  GtkWindow *window;
  GtkWidget *dropdown;
  GtkWidget *days_row;
  GtkWidget *keep_row;
  GtkSpinButton *days_spin;
  GtkSpinButton *keep_spin;
  gboolean suppress_signals;
} ArchiveSettingsDialog;

typedef struct {
  AppState *state;
  GtkWindow *window;
  GtkSpinButton *focus_spin;
  GtkSpinButton *short_spin;
  GtkSpinButton *long_spin;
  GtkSpinButton *interval_spin;
  GtkCheckButton *close_to_tray_check;
  GtkCheckButton *focus_guard_warnings_check;
  GtkSpinButton *focus_guard_interval_spin;
  GtkWidget *focus_guard_list;
  GtkWidget *focus_guard_empty_label;
  GtkWidget *focus_guard_entry;
  GtkWidget *focus_guard_active_label;
  guint focus_guard_active_source;
  gboolean suppress_signals;
} TimerSettingsDialog;

typedef struct {
  GtkWindow *window;
  GtkWidget *list;
  GtkWidget *empty_label;
} ArchivedDialog;

static void archive_settings_update_controls(ArchiveSettingsDialog *dialog);
static void timer_settings_update_controls(TimerSettingsDialog *dialog);
static void focus_guard_settings_update_controls(TimerSettingsDialog *dialog);
static void on_focus_guard_interval_changed(GtkSpinButton *spin,
                                            gpointer user_data);
static void on_focus_guard_warnings_toggled(GtkCheckButton *button,
                                            gpointer user_data);
static void on_focus_guard_add_clicked(GtkButton *button, gpointer user_data);
static void on_focus_guard_entry_activate(GtkEntry *entry, gpointer user_data);
static void on_focus_guard_remove_clicked(GtkButton *button, gpointer user_data);
static void on_focus_guard_use_active_clicked(GtkButton *button,
                                              gpointer user_data);

static GtkWidget *
create_dialog_icon_button(const char *icon_name,
                          int size,
                          const char *tooltip)
{
  GtkWidget *button = gtk_button_new();
  gtk_widget_add_css_class(button, "icon-button");
  gtk_widget_set_size_request(button, 34, 34);
  gtk_widget_set_valign(button, GTK_ALIGN_CENTER);

  GtkWidget *icon = gtk_image_new_from_icon_name(icon_name);
  gtk_image_set_pixel_size(GTK_IMAGE(icon), size);
  gtk_button_set_child(GTK_BUTTON(button), icon);

  if (tooltip != NULL) {
    gtk_widget_set_tooltip_text(button, tooltip);
    gtk_accessible_update_property(GTK_ACCESSIBLE(button),
                                   GTK_ACCESSIBLE_PROPERTY_LABEL,
                                   tooltip,
                                   -1);
  }

  return button;
}

static void
archive_settings_dialog_free(gpointer data)
{
  ArchiveSettingsDialog *dialog = data;
  if (dialog == NULL) {
    return;
  }

  g_free(dialog);
}

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

  g_free(dialog);
}

static void
archived_dialog_free(gpointer data)
{
  ArchivedDialog *dialog = data;
  if (dialog == NULL) {
    return;
  }

  g_free(dialog);
}

static void
confirm_dialog_free(gpointer data)
{
  ConfirmDialog *dialog = data;
  if (dialog == NULL) {
    return;
  }

  g_free(dialog);
}

static void
apply_confirm_dialog(ConfirmDialog *dialog)
{
  if (dialog == NULL || dialog->state == NULL || dialog->state->store == NULL ||
      dialog->task == NULL) {
    return;
  }

  if (dialog->action == DIALOG_CONFIRM_ACTIVATE_TASK) {
    task_store_set_active(dialog->state->store, dialog->task);
  } else {
    task_store_complete(dialog->state->store, dialog->task);
  }

  task_store_apply_archive_policy(dialog->state->store);
  task_list_save_store(dialog->state);
  task_list_refresh(dialog->state);

  gtk_window_destroy(GTK_WINDOW(dialog->window));
}

static void
on_confirm_ok_clicked(GtkButton *button, gpointer user_data)
{
  (void)button;
  apply_confirm_dialog((ConfirmDialog *)user_data);
}

static void
on_confirm_cancel_clicked(GtkButton *button, gpointer user_data)
{
  (void)button;
  gtk_window_destroy(GTK_WINDOW(user_data));
}

void
dialogs_show_confirm(AppState *state,
                     const char *title_text,
                     const char *body_text,
                     PomodoroTask *task,
                     DialogConfirmAction action)
{
  if (state == NULL || task == NULL) {
    return;
  }

  GtkWidget *dialog = gtk_window_new();
  gtk_window_set_title(GTK_WINDOW(dialog), title_text);
  gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
  gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);
  gtk_window_set_transient_for(GTK_WINDOW(dialog), state->window);
  gtk_window_set_default_size(GTK_WINDOW(dialog), 420, 180);

  GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_top(root, 16);
  gtk_widget_set_margin_bottom(root, 16);
  gtk_widget_set_margin_start(root, 16);
  gtk_widget_set_margin_end(root, 16);

  GtkWidget *title = gtk_label_new(title_text);
  gtk_widget_add_css_class(title, "card-title");
  gtk_widget_set_halign(title, GTK_ALIGN_START);

  GtkWidget *body = gtk_label_new(body_text);
  gtk_widget_add_css_class(body, "task-meta");
  gtk_widget_set_halign(body, GTK_ALIGN_START);
  gtk_label_set_wrap(GTK_LABEL(body), TRUE);

  GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_halign(actions, GTK_ALIGN_END);

  GtkWidget *cancel = gtk_button_new_with_label("Cancel");
  gtk_widget_add_css_class(cancel, "btn-secondary");
  gtk_widget_add_css_class(cancel, "btn-compact");

  GtkWidget *confirm = gtk_button_new_with_label("Confirm");
  gtk_widget_add_css_class(confirm, "btn-primary");
  gtk_widget_add_css_class(confirm, "btn-compact");

  gtk_box_append(GTK_BOX(actions), cancel);
  gtk_box_append(GTK_BOX(actions), confirm);

  gtk_box_append(GTK_BOX(root), title);
  gtk_box_append(GTK_BOX(root), body);
  gtk_box_append(GTK_BOX(root), actions);

  gtk_window_set_child(GTK_WINDOW(dialog), root);

  ConfirmDialog *dialog_state = g_new0(ConfirmDialog, 1);
  dialog_state->state = state;
  dialog_state->task = task;
  dialog_state->window = dialog;
  dialog_state->action = action;

  g_object_set_data_full(G_OBJECT(dialog),
                         "confirm-dialog",
                         dialog_state,
                         confirm_dialog_free);

  g_signal_connect(cancel,
                   "clicked",
                   G_CALLBACK(on_confirm_cancel_clicked),
                   dialog);
  g_signal_connect(confirm,
                   "clicked",
                   G_CALLBACK(on_confirm_ok_clicked),
                   dialog_state);

  gtk_window_present(GTK_WINDOW(dialog));
}

static void
on_archive_settings_window_destroy(GtkWidget *widget, gpointer user_data)
{
  (void)widget;
  ArchiveSettingsDialog *dialog = user_data;
  if (dialog == NULL) {
    return;
  }

  dialog->suppress_signals = TRUE;
  g_info("Archive settings window destroyed");
  if (dialog->state != NULL) {
    dialog->state->archive_settings_window = NULL;
  }
}

static gboolean
on_archive_settings_window_close(GtkWindow *window, gpointer user_data)
{
  (void)window;
  ArchiveSettingsDialog *dialog = user_data;
  if (dialog != NULL) {
    dialog->suppress_signals = TRUE;
    g_info("Archive settings window close requested");
  }
  return FALSE;
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
on_archived_window_destroy(GtkWidget *widget, gpointer user_data)
{
  (void)widget;
  AppState *state = user_data;
  if (state == NULL) {
    return;
  }

  g_info("Archived window destroyed");
  state->archived_window = NULL;
}

static void
on_archive_settings_strategy_changed(GObject *object,
                                     GParamSpec *pspec,
                                     gpointer user_data)
{
  (void)object;
  (void)pspec;

  ArchiveSettingsDialog *dialog = user_data;
  if (dialog == NULL || dialog->suppress_signals || dialog->state == NULL ||
      dialog->state->store == NULL) {
    return;
  }

  TaskArchiveStrategy strategy =
      task_store_get_archive_strategy(dialog->state->store);

  if (dialog->dropdown != NULL) {
    guint selected =
        gtk_drop_down_get_selected(GTK_DROP_DOWN(dialog->dropdown));
    if (selected == 1) {
      strategy.type = TASK_ARCHIVE_IMMEDIATE;
    } else if (selected == 2) {
      strategy.type = TASK_ARCHIVE_KEEP_LATEST;
    } else {
      strategy.type = TASK_ARCHIVE_AFTER_DAYS;
    }
  }

  if (dialog->days_spin != NULL) {
    strategy.days = (guint)gtk_spin_button_get_value_as_int(dialog->days_spin);
  }
  if (dialog->keep_spin != NULL) {
    strategy.keep_latest =
        (guint)gtk_spin_button_get_value_as_int(dialog->keep_spin);
  }

  task_store_set_archive_strategy(dialog->state->store, strategy);
  task_store_apply_archive_policy(dialog->state->store);
  task_list_save_store(dialog->state);
  task_list_refresh(dialog->state);
  archive_settings_update_controls(dialog);
}

static void
on_archive_settings_value_changed(GtkSpinButton *spin, gpointer user_data)
{
  (void)spin;

  ArchiveSettingsDialog *dialog = user_data;
  if (dialog == NULL || dialog->suppress_signals || dialog->state == NULL ||
      dialog->state->store == NULL) {
    return;
  }

  on_archive_settings_strategy_changed(NULL, NULL, dialog);
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
focus_guard_update_empty_label(TimerSettingsDialog *dialog)
{
  if (dialog == NULL || dialog->focus_guard_empty_label == NULL ||
      dialog->focus_guard_list == NULL) {
    return;
  }

  gboolean has_rows = gtk_widget_get_first_child(dialog->focus_guard_list) != NULL;
  gtk_widget_set_visible(dialog->focus_guard_empty_label, !has_rows);
}

static gboolean
focus_guard_blacklist_contains(TimerSettingsDialog *dialog, const char *value)
{
  if (dialog == NULL || dialog->focus_guard_list == NULL || value == NULL) {
    return FALSE;
  }

  char *needle = g_ascii_strdown(value, -1);
  GtkWidget *child = gtk_widget_get_first_child(dialog->focus_guard_list);
  while (child != NULL) {
    char *key = g_object_get_data(G_OBJECT(child), "blacklist-key");
    if (key != NULL && g_strcmp0(key, needle) == 0) {
      g_free(needle);
      return TRUE;
    }
    child = gtk_widget_get_next_sibling(child);
  }

  g_free(needle);
  return FALSE;
}

static void
focus_guard_append_blacklist_row(TimerSettingsDialog *dialog, const char *value)
{
  if (dialog == NULL || dialog->focus_guard_list == NULL || value == NULL) {
    return;
  }

  GtkWidget *row = gtk_list_box_row_new();
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_hexpand(box, TRUE);

  GtkWidget *label = gtk_label_new(value);
  gtk_widget_add_css_class(label, "focus-guard-app");
  gtk_widget_set_halign(label, GTK_ALIGN_START);
  gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
  gtk_widget_set_hexpand(label, TRUE);

  GtkWidget *remove_button = gtk_button_new();
  gtk_widget_add_css_class(remove_button, "icon-button");
  GtkWidget *remove_icon =
      gtk_image_new_from_icon_name("pomodoro-delete-symbolic");
  gtk_image_set_pixel_size(GTK_IMAGE(remove_icon), 16);
  gtk_button_set_child(GTK_BUTTON(remove_button), remove_icon);
  gtk_widget_set_tooltip_text(remove_button, "Remove");

  gtk_box_append(GTK_BOX(box), label);
  gtk_box_append(GTK_BOX(box), remove_button);
  gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);

  g_object_set_data_full(G_OBJECT(row),
                         "blacklist-value",
                         g_strdup(value),
                         g_free);
  g_object_set_data_full(G_OBJECT(row),
                         "blacklist-key",
                         g_ascii_strdown(value, -1),
                         g_free);
  g_object_set_data(G_OBJECT(remove_button), "blacklist-row", row);
  g_signal_connect(remove_button,
                   "clicked",
                   G_CALLBACK(on_focus_guard_remove_clicked),
                   dialog);

  gtk_list_box_append(GTK_LIST_BOX(dialog->focus_guard_list), row);
}

static void
focus_guard_clear_list(GtkWidget *list)
{
  if (list == NULL) {
    return;
  }

  GtkWidget *child = gtk_widget_get_first_child(list);
  while (child != NULL) {
    GtkWidget *next = gtk_widget_get_next_sibling(child);
    gtk_list_box_remove(GTK_LIST_BOX(list), child);
    child = next;
  }
}

static char **
focus_guard_collect_blacklist(TimerSettingsDialog *dialog)
{
  GPtrArray *items = g_ptr_array_new_with_free_func(g_free);
  if (dialog == NULL || dialog->focus_guard_list == NULL) {
    g_ptr_array_add(items, NULL);
    return (char **)g_ptr_array_free(items, FALSE);
  }

  GtkWidget *child = gtk_widget_get_first_child(dialog->focus_guard_list);
  while (child != NULL) {
    char *value = g_object_get_data(G_OBJECT(child), "blacklist-value");
    if (value != NULL && *value != '\0') {
      g_ptr_array_add(items, g_strdup(value));
    }
    child = gtk_widget_get_next_sibling(child);
  }

  g_ptr_array_add(items, NULL);
  return (char **)g_ptr_array_free(items, FALSE);
}

static void
focus_guard_apply_settings(TimerSettingsDialog *dialog)
{
  if (dialog == NULL || dialog->suppress_signals || dialog->state == NULL ||
      dialog->state->focus_guard == NULL) {
    return;
  }

  FocusGuardConfig config = focus_guard_get_config(dialog->state->focus_guard);

  if (dialog->focus_guard_interval_spin != NULL) {
    config.detection_interval_seconds =
        (guint)gtk_spin_button_get_value_as_int(dialog->focus_guard_interval_spin);
  }

  if (dialog->focus_guard_warnings_check != NULL) {
    config.warnings_enabled =
        gtk_check_button_get_active(dialog->focus_guard_warnings_check);
  }

  g_strfreev(config.blacklist);
  config.blacklist = focus_guard_collect_blacklist(dialog);
  focus_guard_config_normalize(&config);

  focus_guard_apply_config(dialog->state->focus_guard, config);

  GError *error = NULL;
  if (!settings_storage_save_focus_guard(&config, &error)) {
    g_warning("Failed to save focus guard settings: %s",
              error ? error->message : "unknown error");
    g_clear_error(&error);
  }

  focus_guard_config_clear(&config);
}

static gboolean
focus_guard_update_active_label(gpointer user_data)
{
  TimerSettingsDialog *dialog = user_data;
  if (dialog == NULL || dialog->focus_guard_active_label == NULL) {
    return G_SOURCE_REMOVE;
  }

  char *app_name = NULL;
  if (focus_guard_x11_get_active_app(&app_name, NULL) && app_name != NULL) {
    char *text = g_strdup_printf("Active app: %s", app_name);
    gtk_label_set_text(GTK_LABEL(dialog->focus_guard_active_label), text);
    g_free(text);
  } else {
    gtk_label_set_text(GTK_LABEL(dialog->focus_guard_active_label),
                       "Active app: unavailable");
  }

  g_free(app_name);

  return G_SOURCE_CONTINUE;
}

static void
focus_guard_start_active_monitor(TimerSettingsDialog *dialog)
{
  if (dialog == NULL || dialog->focus_guard_active_source != 0) {
    return;
  }

  dialog->focus_guard_active_source =
      g_timeout_add_seconds(1, focus_guard_update_active_label, dialog);
  focus_guard_update_active_label(dialog);
}

static void
focus_guard_settings_update_controls(TimerSettingsDialog *dialog)
{
  if (dialog == NULL || dialog->state == NULL || dialog->state->focus_guard == NULL) {
    return;
  }

  FocusGuardConfig config = focus_guard_get_config(dialog->state->focus_guard);

  if (dialog->focus_guard_interval_spin != NULL) {
    gtk_spin_button_set_value(dialog->focus_guard_interval_spin,
                              (gdouble)config.detection_interval_seconds);
  }

  if (dialog->focus_guard_warnings_check != NULL) {
    gtk_check_button_set_active(dialog->focus_guard_warnings_check,
                                config.warnings_enabled);
  }

  if (dialog->focus_guard_list != NULL) {
    focus_guard_clear_list(dialog->focus_guard_list);
    if (config.blacklist != NULL) {
      for (gsize i = 0; config.blacklist[i] != NULL; i++) {
        focus_guard_append_blacklist_row(dialog, config.blacklist[i]);
      }
    }
    focus_guard_update_empty_label(dialog);
  }

  focus_guard_config_clear(&config);
}

static void
on_focus_guard_interval_changed(GtkSpinButton *spin, gpointer user_data)
{
  (void)spin;
  focus_guard_apply_settings((TimerSettingsDialog *)user_data);
}

static void
on_focus_guard_warnings_toggled(GtkCheckButton *button, gpointer user_data)
{
  (void)button;
  focus_guard_apply_settings((TimerSettingsDialog *)user_data);
}

static void
on_focus_guard_add_clicked(GtkButton *button, gpointer user_data)
{
  (void)button;
  TimerSettingsDialog *dialog = user_data;
  if (dialog == NULL || dialog->focus_guard_entry == NULL) {
    return;
  }

  const char *raw = gtk_editable_get_text(GTK_EDITABLE(dialog->focus_guard_entry));
  if (raw == NULL) {
    return;
  }

  char *trimmed = g_strstrip(g_strdup(raw));
  if (*trimmed == '\0') {
    g_free(trimmed);
    return;
  }

  if (focus_guard_blacklist_contains(dialog, trimmed)) {
    g_free(trimmed);
    gtk_editable_set_text(GTK_EDITABLE(dialog->focus_guard_entry), "");
    return;
  }

  focus_guard_append_blacklist_row(dialog, trimmed);
  gtk_editable_set_text(GTK_EDITABLE(dialog->focus_guard_entry), "");
  focus_guard_update_empty_label(dialog);
  focus_guard_apply_settings(dialog);
  g_free(trimmed);
}

static void
on_focus_guard_entry_activate(GtkEntry *entry, gpointer user_data)
{
  (void)entry;
  on_focus_guard_add_clicked(NULL, user_data);
}

static void
on_focus_guard_use_active_clicked(GtkButton *button, gpointer user_data)
{
  (void)button;
  TimerSettingsDialog *dialog = user_data;
  if (dialog == NULL || dialog->focus_guard_entry == NULL) {
    return;
  }

  char *app_name = NULL;
  if (focus_guard_x11_get_active_app(&app_name, NULL) && app_name != NULL) {
    gtk_editable_set_text(GTK_EDITABLE(dialog->focus_guard_entry), app_name);
    on_focus_guard_add_clicked(NULL, dialog);
  }

  g_free(app_name);
}

static void
on_focus_guard_remove_clicked(GtkButton *button, gpointer user_data)
{
  TimerSettingsDialog *dialog = user_data;
  if (dialog == NULL || button == NULL || dialog->focus_guard_list == NULL) {
    return;
  }

  GtkWidget *row = g_object_get_data(G_OBJECT(button), "blacklist-row");
  if (row != NULL) {
    gtk_list_box_remove(GTK_LIST_BOX(dialog->focus_guard_list), row);
    focus_guard_update_empty_label(dialog);
    focus_guard_apply_settings(dialog);
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
archive_settings_update_controls(ArchiveSettingsDialog *dialog)
{
  if (dialog == NULL || dialog->state == NULL) {
    return;
  }

  dialog->suppress_signals = TRUE;

  TaskArchiveStrategy strategy =
      task_store_get_archive_strategy(dialog->state->store);

  if (dialog->dropdown != NULL) {
    guint selected = 0;
    switch (strategy.type) {
      case TASK_ARCHIVE_AFTER_DAYS:
        selected = 0;
        break;
      case TASK_ARCHIVE_IMMEDIATE:
        selected = 1;
        break;
      case TASK_ARCHIVE_KEEP_LATEST:
        selected = 2;
        break;
    }
    gtk_drop_down_set_selected(GTK_DROP_DOWN(dialog->dropdown), selected);
  }

  if (dialog->days_spin != NULL) {
    gtk_spin_button_set_value(dialog->days_spin, (gdouble)strategy.days);
  }
  if (dialog->keep_spin != NULL) {
    gtk_spin_button_set_value(dialog->keep_spin,
                              (gdouble)strategy.keep_latest);
  }

  if (dialog->days_row != NULL) {
    gtk_widget_set_visible(dialog->days_row,
                           strategy.type == TASK_ARCHIVE_AFTER_DAYS);
  }
  if (dialog->keep_row != NULL) {
    gtk_widget_set_visible(dialog->keep_row,
                           strategy.type == TASK_ARCHIVE_KEEP_LATEST);
  }

  dialog->suppress_signals = FALSE;
}

static void
show_archive_settings_window(AppState *state)
{
  if (state == NULL) {
    return;
  }

  if (state->archive_settings_window != NULL) {
    gtk_window_present(state->archive_settings_window);
    return;
  }

  GtkApplication *app = gtk_window_get_application(state->window);
  GtkWidget *window = gtk_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(window), "Archive Settings");
  if (state->archived_window != NULL) {
    gtk_window_set_transient_for(GTK_WINDOW(window), state->archived_window);
  } else {
    gtk_window_set_transient_for(GTK_WINDOW(window), state->window);
  }
  gtk_window_set_modal(GTK_WINDOW(window), TRUE);
  gtk_window_set_default_size(GTK_WINDOW(window), 420, 260);

  GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_top(root, 18);
  gtk_widget_set_margin_bottom(root, 18);
  gtk_widget_set_margin_start(root, 18);
  gtk_widget_set_margin_end(root, 18);

  GtkWidget *title = gtk_label_new("Archive rules");
  gtk_widget_add_css_class(title, "card-title");
  gtk_widget_set_halign(title, GTK_ALIGN_START);

  GtkWidget *desc =
      gtk_label_new("Completed tasks archive automatically to keep the list tidy.");
  gtk_widget_add_css_class(desc, "task-meta");
  gtk_widget_set_halign(desc, GTK_ALIGN_START);
  gtk_label_set_wrap(GTK_LABEL(desc), TRUE);

  const char *archive_options[] = {
      "Archive after N days",
      "Archive immediately",
      "Keep latest N completed",
      NULL};

  GtkWidget *archive_dropdown = gtk_drop_down_new_from_strings(archive_options);
  gtk_widget_add_css_class(archive_dropdown, "archive-dropdown");
  gtk_widget_set_hexpand(archive_dropdown, TRUE);

  GtkWidget *archive_days_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  GtkWidget *archive_days_label = gtk_label_new("Days to keep");
  gtk_widget_add_css_class(archive_days_label, "setting-label");
  gtk_widget_set_halign(archive_days_label, GTK_ALIGN_START);
  gtk_widget_set_hexpand(archive_days_label, TRUE);
  GtkWidget *archive_days_spin = gtk_spin_button_new_with_range(1, 90, 1);
  gtk_widget_set_halign(archive_days_spin, GTK_ALIGN_END);

  gtk_box_append(GTK_BOX(archive_days_row), archive_days_label);
  gtk_box_append(GTK_BOX(archive_days_row), archive_days_spin);

  GtkWidget *archive_keep_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  GtkWidget *archive_keep_label = gtk_label_new("Keep latest");
  gtk_widget_add_css_class(archive_keep_label, "setting-label");
  gtk_widget_set_halign(archive_keep_label, GTK_ALIGN_START);
  gtk_widget_set_hexpand(archive_keep_label, TRUE);
  GtkWidget *archive_keep_spin = gtk_spin_button_new_with_range(1, 50, 1);
  gtk_widget_set_halign(archive_keep_spin, GTK_ALIGN_END);

  gtk_box_append(GTK_BOX(archive_keep_row), archive_keep_label);
  gtk_box_append(GTK_BOX(archive_keep_row), archive_keep_spin);

  GtkWidget *hint =
      gtk_label_new("Changes apply immediately and can be adjusted anytime.");
  gtk_widget_add_css_class(hint, "task-meta");
  gtk_widget_set_halign(hint, GTK_ALIGN_START);
  gtk_label_set_wrap(GTK_LABEL(hint), TRUE);

  gtk_box_append(GTK_BOX(root), title);
  gtk_box_append(GTK_BOX(root), desc);
  gtk_box_append(GTK_BOX(root), archive_dropdown);
  gtk_box_append(GTK_BOX(root), archive_days_row);
  gtk_box_append(GTK_BOX(root), archive_keep_row);
  gtk_box_append(GTK_BOX(root), hint);

  gtk_window_set_child(GTK_WINDOW(window), root);
  state->archive_settings_window = GTK_WINDOW(window);

  ArchiveSettingsDialog *dialog = g_new0(ArchiveSettingsDialog, 1);
  dialog->state = state;
  dialog->window = GTK_WINDOW(window);
  dialog->dropdown = archive_dropdown;
  dialog->days_row = archive_days_row;
  dialog->keep_row = archive_keep_row;
  dialog->days_spin = GTK_SPIN_BUTTON(archive_days_spin);
  dialog->keep_spin = GTK_SPIN_BUTTON(archive_keep_spin);

  g_signal_connect(archive_dropdown,
                   "notify::selected",
                   G_CALLBACK(on_archive_settings_strategy_changed),
                   dialog);
  g_signal_connect(archive_days_spin,
                   "value-changed",
                   G_CALLBACK(on_archive_settings_value_changed),
                   dialog);
  g_signal_connect(archive_keep_spin,
                   "value-changed",
                   G_CALLBACK(on_archive_settings_value_changed),
                   dialog);

  g_object_set_data_full(G_OBJECT(window),
                         "archive-settings-dialog",
                         dialog,
                         archive_settings_dialog_free);

  g_signal_connect(window,
                   "close-request",
                   G_CALLBACK(on_archive_settings_window_close),
                   dialog);
  g_signal_connect(window,
                   "destroy",
                   G_CALLBACK(on_archive_settings_window_destroy),
                   dialog);

  archive_settings_update_controls(dialog);
  gtk_window_present(GTK_WINDOW(window));
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

  GtkWidget *divider = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);

  GtkWidget *guard_title = gtk_label_new("Focus guard");
  gtk_widget_add_css_class(guard_title, "card-title");
  gtk_widget_set_halign(guard_title, GTK_ALIGN_START);

  GtkWidget *guard_desc = gtk_label_new(
      "Warn when blacklisted apps take focus during a running session.");
  gtk_widget_add_css_class(guard_desc, "task-meta");
  gtk_widget_set_halign(guard_desc, GTK_ALIGN_START);
  gtk_label_set_wrap(GTK_LABEL(guard_desc), TRUE);

  GtkWidget *guard_warning_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  GtkWidget *guard_warning_label = gtk_label_new("Warnings");
  gtk_widget_add_css_class(guard_warning_label, "setting-label");
  gtk_widget_set_halign(guard_warning_label, GTK_ALIGN_START);
  gtk_widget_set_hexpand(guard_warning_label, TRUE);
  GtkWidget *guard_warning_check = gtk_check_button_new();
  gtk_widget_set_halign(guard_warning_check, GTK_ALIGN_END);
  gtk_box_append(GTK_BOX(guard_warning_row), guard_warning_label);
  gtk_box_append(GTK_BOX(guard_warning_row), guard_warning_check);

  GtkWidget *guard_interval_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  GtkWidget *guard_interval_label = gtk_label_new("Check interval (sec)");
  gtk_widget_add_css_class(guard_interval_label, "setting-label");
  gtk_widget_set_halign(guard_interval_label, GTK_ALIGN_START);
  gtk_widget_set_hexpand(guard_interval_label, TRUE);
  GtkWidget *guard_interval_spin = gtk_spin_button_new_with_range(1, 60, 1);
  gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(guard_interval_spin), TRUE);
  gtk_widget_add_css_class(guard_interval_spin, "setting-spin");
  gtk_widget_set_halign(guard_interval_spin, GTK_ALIGN_END);
  gtk_box_append(GTK_BOX(guard_interval_row), guard_interval_label);
  gtk_box_append(GTK_BOX(guard_interval_row), guard_interval_spin);

  GtkWidget *guard_entry_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_hexpand(guard_entry_row, TRUE);
  GtkWidget *guard_entry = gtk_entry_new();
  gtk_widget_set_hexpand(guard_entry, TRUE);
  gtk_entry_set_placeholder_text(GTK_ENTRY(guard_entry),
                                 "Add app name (e.g. Discord, Chrome)");
  gtk_widget_add_css_class(guard_entry, "task-entry");

  GtkWidget *guard_add_button = gtk_button_new();
  gtk_widget_add_css_class(guard_add_button, "icon-button");
  gtk_widget_set_size_request(guard_add_button, 32, 32);
  GtkWidget *guard_add_icon =
      gtk_image_new_from_icon_name("list-add-symbolic");
  gtk_image_set_pixel_size(GTK_IMAGE(guard_add_icon), 18);
  gtk_button_set_child(GTK_BUTTON(guard_add_button), guard_add_icon);
  gtk_widget_set_tooltip_text(guard_add_button, "Add to blacklist");

  gtk_box_append(GTK_BOX(guard_entry_row), guard_entry);
  gtk_box_append(GTK_BOX(guard_entry_row), guard_add_button);

  GtkWidget *guard_active_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_hexpand(guard_active_row, TRUE);

  GtkWidget *guard_active_label = gtk_label_new("Active app: unavailable");
  gtk_widget_add_css_class(guard_active_label, "task-meta");
  gtk_widget_set_halign(guard_active_label, GTK_ALIGN_START);
  gtk_label_set_ellipsize(GTK_LABEL(guard_active_label), PANGO_ELLIPSIZE_END);
  gtk_widget_set_hexpand(guard_active_label, TRUE);

  GtkWidget *guard_use_button = gtk_button_new_with_label("Use active app");
  gtk_widget_add_css_class(guard_use_button, "btn-secondary");
  gtk_widget_add_css_class(guard_use_button, "btn-compact");
  gtk_widget_set_halign(guard_use_button, GTK_ALIGN_END);
  gtk_widget_set_tooltip_text(guard_use_button,
                              "Add the currently focused app to the blacklist");

  gtk_box_append(GTK_BOX(guard_active_row), guard_active_label);
  gtk_box_append(GTK_BOX(guard_active_row), guard_use_button);

  GtkWidget *guard_list = gtk_list_box_new();
  gtk_widget_add_css_class(guard_list, "focus-guard-list");
  gtk_list_box_set_selection_mode(GTK_LIST_BOX(guard_list),
                                  GTK_SELECTION_NONE);

  GtkWidget *guard_scroller = gtk_scrolled_window_new();
  gtk_widget_add_css_class(guard_scroller, "task-scroller");
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(guard_scroller),
                                 GTK_POLICY_NEVER,
                                 GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_min_content_height(
      GTK_SCROLLED_WINDOW(guard_scroller),
      120);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(guard_scroller), guard_list);

  GtkWidget *guard_empty_label = gtk_label_new("No blacklisted apps yet.");
  gtk_widget_add_css_class(guard_empty_label, "focus-guard-empty");
  gtk_widget_set_halign(guard_empty_label, GTK_ALIGN_START);
  gtk_label_set_wrap(GTK_LABEL(guard_empty_label), TRUE);

  GtkWidget *hint =
      gtk_label_new("Changes apply immediately and can be adjusted anytime.");
  gtk_widget_add_css_class(hint, "task-meta");
  gtk_widget_set_halign(hint, GTK_ALIGN_START);
  gtk_label_set_wrap(GTK_LABEL(hint), TRUE);

  gtk_box_append(GTK_BOX(root), title);
  gtk_box_append(GTK_BOX(root), desc);
  gtk_box_append(GTK_BOX(root), focus_row);
  gtk_box_append(GTK_BOX(root), short_row);
  gtk_box_append(GTK_BOX(root), long_row);
  gtk_box_append(GTK_BOX(root), interval_row);
  gtk_box_append(GTK_BOX(root), tray_row);
  gtk_box_append(GTK_BOX(root), divider);
  gtk_box_append(GTK_BOX(root), guard_title);
  gtk_box_append(GTK_BOX(root), guard_desc);
  gtk_box_append(GTK_BOX(root), guard_warning_row);
  gtk_box_append(GTK_BOX(root), guard_interval_row);
  gtk_box_append(GTK_BOX(root), guard_entry_row);
  gtk_box_append(GTK_BOX(root), guard_active_row);
  gtk_box_append(GTK_BOX(root), guard_scroller);
  gtk_box_append(GTK_BOX(root), guard_empty_label);
  gtk_box_append(GTK_BOX(root), hint);

  gtk_window_set_child(GTK_WINDOW(window), root);
  state->timer_settings_window = GTK_WINDOW(window);

  TimerSettingsDialog *dialog = g_new0(TimerSettingsDialog, 1);
  dialog->state = state;
  dialog->window = GTK_WINDOW(window);
  dialog->focus_spin = GTK_SPIN_BUTTON(focus_spin);
  dialog->short_spin = GTK_SPIN_BUTTON(short_spin);
  dialog->long_spin = GTK_SPIN_BUTTON(long_spin);
  dialog->interval_spin = GTK_SPIN_BUTTON(interval_spin);
  dialog->close_to_tray_check = GTK_CHECK_BUTTON(tray_check);
  dialog->focus_guard_warnings_check = GTK_CHECK_BUTTON(guard_warning_check);
  dialog->focus_guard_interval_spin = GTK_SPIN_BUTTON(guard_interval_spin);
  dialog->focus_guard_list = guard_list;
  dialog->focus_guard_empty_label = guard_empty_label;
  dialog->focus_guard_entry = guard_entry;
  dialog->focus_guard_active_label = guard_active_label;

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
  g_signal_connect(guard_interval_spin,
                   "value-changed",
                   G_CALLBACK(on_focus_guard_interval_changed),
                   dialog);
  g_signal_connect(guard_warning_check,
                   "toggled",
                   G_CALLBACK(on_focus_guard_warnings_toggled),
                   dialog);
  g_signal_connect(guard_add_button,
                   "clicked",
                   G_CALLBACK(on_focus_guard_add_clicked),
                   dialog);
  g_signal_connect(guard_entry,
                   "activate",
                   G_CALLBACK(on_focus_guard_entry_activate),
                   dialog);
  g_signal_connect(guard_use_button,
                   "clicked",
                   G_CALLBACK(on_focus_guard_use_active_clicked),
                   dialog);

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

static ArchivedDialog *
archived_dialog_get(AppState *state)
{
  if (state == NULL || state->archived_window == NULL) {
    return NULL;
  }

  return g_object_get_data(G_OBJECT(state->archived_window), "archived-dialog");
}

static void
show_archived_window(AppState *state)
{
  if (state == NULL) {
    return;
  }

  if (state->archived_window != NULL) {
    gtk_window_present(state->archived_window);
    return;
  }

  GtkApplication *app = gtk_window_get_application(state->window);
  GtkWidget *window = gtk_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(window), "Archived Tasks");
  gtk_window_set_transient_for(GTK_WINDOW(window), state->window);
  gtk_window_set_default_size(GTK_WINDOW(window), 520, 420);

  GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_top(root, 18);
  gtk_widget_set_margin_bottom(root, 18);
  gtk_widget_set_margin_start(root, 18);
  gtk_widget_set_margin_end(root, 18);

  GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_hexpand(header, TRUE);
  gtk_widget_set_halign(header, GTK_ALIGN_FILL);

  GtkWidget *title = gtk_label_new("Archived tasks");
  gtk_widget_add_css_class(title, "card-title");
  gtk_widget_set_halign(title, GTK_ALIGN_START);
  gtk_widget_set_hexpand(title, TRUE);

  GtkWidget *settings_button =
      create_dialog_icon_button("pomodoro-edit-symbolic", 18, "Archive settings");
  g_signal_connect(settings_button,
                   "clicked",
                   G_CALLBACK(dialogs_on_show_archive_settings_clicked),
                   state);

  gtk_box_append(GTK_BOX(header), title);
  gtk_box_append(GTK_BOX(header), settings_button);

  GtkWidget *desc =
      gtk_label_new("Restore tasks to bring them back into your active list.");
  gtk_widget_add_css_class(desc, "task-meta");
  gtk_widget_set_halign(desc, GTK_ALIGN_START);
  gtk_label_set_wrap(GTK_LABEL(desc), TRUE);

  GtkWidget *archived_list = gtk_list_box_new();
  gtk_widget_add_css_class(archived_list, "task-list");
  gtk_list_box_set_selection_mode(GTK_LIST_BOX(archived_list),
                                  GTK_SELECTION_NONE);

  GtkWidget *archived_scroller = gtk_scrolled_window_new();
  gtk_widget_add_css_class(archived_scroller, "task-scroller");
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(archived_scroller),
                                 GTK_POLICY_NEVER,
                                 GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_min_content_height(
      GTK_SCROLLED_WINDOW(archived_scroller),
      260);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(archived_scroller),
                                archived_list);

  GtkWidget *archived_empty_label =
      gtk_label_new("No archived tasks yet.");
  gtk_widget_add_css_class(archived_empty_label, "task-empty");
  gtk_widget_set_halign(archived_empty_label, GTK_ALIGN_START);
  gtk_label_set_wrap(GTK_LABEL(archived_empty_label), TRUE);

  gtk_box_append(GTK_BOX(root), header);
  gtk_box_append(GTK_BOX(root), desc);
  gtk_box_append(GTK_BOX(root), archived_scroller);
  gtk_box_append(GTK_BOX(root), archived_empty_label);

  gtk_window_set_child(GTK_WINDOW(window), root);
  state->archived_window = GTK_WINDOW(window);

  ArchivedDialog *dialog = g_new0(ArchivedDialog, 1);
  dialog->window = GTK_WINDOW(window);
  dialog->list = archived_list;
  dialog->empty_label = archived_empty_label;
  g_object_set_data_full(G_OBJECT(window),
                         "archived-dialog",
                         dialog,
                         archived_dialog_free);
  g_signal_connect(window,
                   "destroy",
                   G_CALLBACK(on_archived_window_destroy),
                   state);

  task_list_refresh(state);
  gtk_window_present(GTK_WINDOW(window));
}

void
dialogs_on_show_archive_settings_clicked(GtkButton *button, gpointer user_data)
{
  (void)button;
  show_archive_settings_window((AppState *)user_data);
}

void
dialogs_on_show_timer_settings_clicked(GtkButton *button, gpointer user_data)
{
  (void)button;
  show_timer_settings_window((AppState *)user_data);
}

void
dialogs_on_show_archived_clicked(GtkButton *button, gpointer user_data)
{
  (void)button;
  show_archived_window((AppState *)user_data);
}

void
dialogs_cleanup_archive_settings(AppState *state)
{
  if (state == NULL || state->archive_settings_window == NULL) {
    return;
  }

  ArchiveSettingsDialog *dialog = g_object_get_data(
      G_OBJECT(state->archive_settings_window),
      "archive-settings-dialog");
  if (dialog != NULL) {
    dialog->state = NULL;
  }

  gtk_window_destroy(state->archive_settings_window);
  state->archive_settings_window = NULL;
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

void
dialogs_cleanup_archived(AppState *state)
{
  if (state == NULL || state->archived_window == NULL) {
    return;
  }

  ArchivedDialog *dialog = archived_dialog_get(state);
  if (dialog != NULL) {
    dialog->window = NULL;
    dialog->list = NULL;
    dialog->empty_label = NULL;
  }

  gtk_window_destroy(state->archived_window);
  state->archived_window = NULL;
}

gboolean
dialogs_get_archived_targets(AppState *state,
                             GtkWidget **list,
                             GtkWidget **empty_label)
{
  if (list != NULL) {
    *list = NULL;
  }
  if (empty_label != NULL) {
    *empty_label = NULL;
  }

  ArchivedDialog *dialog = archived_dialog_get(state);
  if (dialog == NULL || dialog->list == NULL || dialog->empty_label == NULL) {
    return FALSE;
  }

  if (list != NULL) {
    *list = dialog->list;
  }
  if (empty_label != NULL) {
    *empty_label = dialog->empty_label;
  }

  return TRUE;
}
