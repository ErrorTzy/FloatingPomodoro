#include "ui/dialogs_focus_guard_internal.h"

#include "config.h"
#include "focus/focus_guard_x11.h"

void
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

gboolean
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

gboolean
focus_guard_is_self_app(const char *app_name)
{
  if (app_name == NULL || *app_name == '\0') {
    return FALSE;
  }

  const char *prg = g_get_prgname();
  const char *candidates[] = {APP_ID, APP_NAME, prg, "floating-pomodoro", NULL};

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

void
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

void
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

char **
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

void
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
  if (trimmed[0] == '\0') {
    g_free(trimmed);
    return;
  }

  if (focus_guard_blacklist_contains(dialog, trimmed)) {
    gtk_editable_set_text(GTK_EDITABLE(dialog->focus_guard_entry), "");
    g_free(trimmed);
    return;
  }

  focus_guard_append_blacklist_row(dialog, trimmed);
  gtk_editable_set_text(GTK_EDITABLE(dialog->focus_guard_entry), "");
  focus_guard_update_empty_label(dialog);
  focus_guard_apply_settings(dialog);
  g_free(trimmed);
}

void
on_focus_guard_entry_activate(GtkEntry *entry, gpointer user_data)
{
  (void)entry;
  on_focus_guard_add_clicked(NULL, user_data);
}

void
on_focus_guard_use_active_clicked(GtkButton *button, gpointer user_data)
{
  (void)button;
  TimerSettingsDialog *dialog = user_data;
  if (dialog == NULL || dialog->focus_guard_entry == NULL) {
    return;
  }

  if (dialog->focus_guard_model != NULL &&
      focus_guard_settings_model_get_last_external(dialog->focus_guard_model) !=
          NULL) {
    gtk_editable_set_text(GTK_EDITABLE(dialog->focus_guard_entry),
                          focus_guard_settings_model_get_last_external(
                              dialog->focus_guard_model));
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

void
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
