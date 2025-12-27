#include "ui/dialogs_timer_settings_internal.h"

#include "config.h"
#include "focus/focus_guard.h"
#include "focus/focus_guard_x11.h"
#include "storage/settings_storage.h"

static void on_focus_guard_interval_changed(GtkSpinButton *spin,
                                            gpointer user_data);
static void on_focus_guard_global_toggled(GtkCheckButton *button,
                                          gpointer user_data);
static void on_focus_guard_warnings_toggled(GtkCheckButton *button,
                                            gpointer user_data);
static void on_focus_guard_add_clicked(GtkButton *button, gpointer user_data);
static void on_focus_guard_entry_activate(GtkEntry *entry, gpointer user_data);
static void on_focus_guard_remove_clicked(GtkButton *button,
                                          gpointer user_data);
static void on_focus_guard_use_active_clicked(GtkButton *button,
                                              gpointer user_data);

static void
focus_guard_update_empty_label(TimerSettingsDialog *dialog)
{
  if (dialog == NULL || dialog->focus_guard_empty_label == NULL ||
      dialog->focus_guard_list == NULL) {
    return;
  }

  gboolean has_rows =
      gtk_widget_get_first_child(dialog->focus_guard_list) != NULL;
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

static char *
focus_guard_normalize_id(const char *value)
{
  if (value == NULL) {
    return g_strdup("");
  }

  const char *cursor = value;
  GString *normalized = g_string_new(NULL);
  for (; *cursor != '\0'; cursor++) {
    if (g_ascii_isalnum(*cursor)) {
      g_string_append_c(normalized, (char)g_ascii_tolower(*cursor));
    }
  }
  return g_string_free(normalized, FALSE);
}

static gboolean
focus_guard_is_self_app(const char *app_name)
{
  if (app_name == NULL || *app_name == '\0') {
    return FALSE;
  }

  const char *prg = g_get_prgname();
  const char *candidates[] = {APP_ID, APP_NAME, prg, "xfce4-floating-pomodoro", NULL};

  char *norm_app = focus_guard_normalize_id(app_name);
  if (norm_app == NULL || *norm_app == '\0') {
    g_free(norm_app);
    return FALSE;
  }

  gboolean match = FALSE;
  for (int i = 0; candidates[i] != NULL; i++) {
    if (candidates[i] == NULL) {
      continue;
    }
    char *norm_candidate = focus_guard_normalize_id(candidates[i]);
    if (norm_candidate != NULL && g_strcmp0(norm_app, norm_candidate) == 0) {
      match = TRUE;
      g_free(norm_candidate);
      break;
    }
    g_free(norm_candidate);
  }

  g_free(norm_app);
  return match;
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

  if (dialog->focus_guard_global_check != NULL) {
    config.global_stats_enabled =
        gtk_check_button_get_active(dialog->focus_guard_global_check);
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
    gboolean is_self = focus_guard_is_self_app(app_name);
    if (!is_self) {
      g_free(dialog->focus_guard_last_external);
      dialog->focus_guard_last_external = g_strdup(app_name);
    }

    if (!is_self) {
      char *text = g_strdup_printf("Active app: %s", app_name);
      gtk_label_set_text(GTK_LABEL(dialog->focus_guard_active_label), text);
      g_free(text);
    } else if (dialog->focus_guard_last_external != NULL) {
      char *text = g_strdup_printf("Last active app: %s",
                                   dialog->focus_guard_last_external);
      gtk_label_set_text(GTK_LABEL(dialog->focus_guard_active_label), text);
      g_free(text);
    } else {
      gtk_label_set_text(GTK_LABEL(dialog->focus_guard_active_label),
                         "Last active app: none yet");
    }
  } else if (dialog->focus_guard_last_external != NULL) {
    char *text = g_strdup_printf("Last active app: %s",
                                 dialog->focus_guard_last_external);
    gtk_label_set_text(GTK_LABEL(dialog->focus_guard_active_label), text);
    g_free(text);
  } else {
    gtk_label_set_text(GTK_LABEL(dialog->focus_guard_active_label),
                       "Last active app: unavailable");
  }

  g_free(app_name);

  return G_SOURCE_CONTINUE;
}

void
focus_guard_start_active_monitor(TimerSettingsDialog *dialog)
{
  if (dialog == NULL || dialog->focus_guard_active_source != 0) {
    return;
  }

  dialog->focus_guard_active_source =
      g_timeout_add_seconds(1, focus_guard_update_active_label, dialog);
  focus_guard_update_active_label(dialog);
}

void
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

  if (dialog->focus_guard_global_check != NULL) {
    gtk_check_button_set_active(dialog->focus_guard_global_check,
                                config.global_stats_enabled);
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
on_focus_guard_global_toggled(GtkCheckButton *button, gpointer user_data)
{
  (void)button;
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

  if (dialog->focus_guard_last_external != NULL) {
    gtk_editable_set_text(GTK_EDITABLE(dialog->focus_guard_entry),
                          dialog->focus_guard_last_external);
    on_focus_guard_add_clicked(NULL, dialog);
    return;
  }

  char *app_name = NULL;
  if (focus_guard_x11_get_active_app(&app_name, NULL) && app_name != NULL) {
    if (!focus_guard_is_self_app(app_name)) {
      gtk_editable_set_text(GTK_EDITABLE(dialog->focus_guard_entry), app_name);
      on_focus_guard_add_clicked(NULL, dialog);
    }
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

void
focus_guard_settings_append(TimerSettingsDialog *dialog, GtkWidget *root)
{
  if (dialog == NULL || root == NULL) {
    return;
  }

  GtkWidget *divider = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);

  GtkWidget *guard_title = gtk_label_new("Focus guard");
  gtk_widget_add_css_class(guard_title, "card-title");
  gtk_widget_set_halign(guard_title, GTK_ALIGN_START);

  GtkWidget *guard_desc = gtk_label_new(
      "Warn when blacklisted apps take focus during a running session.");
  gtk_widget_add_css_class(guard_desc, "task-meta");
  gtk_widget_set_halign(guard_desc, GTK_ALIGN_START);
  gtk_label_set_wrap(GTK_LABEL(guard_desc), TRUE);

  GtkWidget *guard_global_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  GtkWidget *guard_global_label = gtk_label_new("Global app usage stats");
  gtk_widget_add_css_class(guard_global_label, "setting-label");
  gtk_widget_set_halign(guard_global_label, GTK_ALIGN_START);
  gtk_widget_set_hexpand(guard_global_label, TRUE);
  GtkWidget *guard_global_check = gtk_check_button_new();
  gtk_widget_set_halign(guard_global_check, GTK_ALIGN_END);
  gtk_widget_set_tooltip_text(guard_global_check,
                              "Track app usage continuously while the app runs.");
  gtk_box_append(GTK_BOX(guard_global_row), guard_global_label);
  gtk_box_append(GTK_BOX(guard_global_row), guard_global_check);

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

  dialog->focus_guard_global_check = GTK_CHECK_BUTTON(guard_global_check);
  dialog->focus_guard_warnings_check = GTK_CHECK_BUTTON(guard_warning_check);
  dialog->focus_guard_interval_spin = GTK_SPIN_BUTTON(guard_interval_spin);
  dialog->focus_guard_list = guard_list;
  dialog->focus_guard_empty_label = guard_empty_label;
  dialog->focus_guard_entry = guard_entry;
  dialog->focus_guard_active_label = guard_active_label;

  g_signal_connect(guard_interval_spin,
                   "value-changed",
                   G_CALLBACK(on_focus_guard_interval_changed),
                   dialog);
  g_signal_connect(guard_global_check,
                   "toggled",
                   G_CALLBACK(on_focus_guard_global_toggled),
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

  gtk_box_append(GTK_BOX(root), divider);
  gtk_box_append(GTK_BOX(root), guard_title);
  gtk_box_append(GTK_BOX(root), guard_desc);
  gtk_box_append(GTK_BOX(root), guard_global_row);
  gtk_box_append(GTK_BOX(root), guard_warning_row);
  gtk_box_append(GTK_BOX(root), guard_interval_row);
  gtk_box_append(GTK_BOX(root), guard_entry_row);
  gtk_box_append(GTK_BOX(root), guard_active_row);
  gtk_box_append(GTK_BOX(root), guard_scroller);
  gtk_box_append(GTK_BOX(root), guard_empty_label);
  gtk_box_append(GTK_BOX(root), hint);
}
