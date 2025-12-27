#include "ui/dialogs_timer_settings_internal.h"

#include "config.h"
#include "focus/focus_guard.h"
#include "focus/focus_guard_x11.h"
#include "focus/ollama_client.h"
#include "focus/trafilatura_client.h"
#include "storage/settings_storage.h"

typedef struct {
  GWeakRef window_ref;
} FocusGuardOllamaRefreshContext;

static void on_focus_guard_interval_changed(GtkSpinButton *spin,
                                            gpointer user_data);
static void on_focus_guard_global_toggled(GtkCheckButton *button,
                                          gpointer user_data);
static void on_focus_guard_warnings_toggled(GtkCheckButton *button,
                                            gpointer user_data);
static void focus_guard_apply_settings(TimerSettingsDialog *dialog);
static void on_focus_guard_chrome_toggled(GtkCheckButton *button,
                                          gpointer user_data);
static void on_focus_guard_chrome_port_changed(GtkSpinButton *spin,
                                               gpointer user_data);
static void on_focus_guard_model_changed(GObject *object,
                                         GParamSpec *pspec,
                                         gpointer user_data);
static void on_focus_guard_ollama_refresh_clicked(GtkButton *button,
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
focus_guard_set_ollama_status(TimerSettingsDialog *dialog, const char *text)
{
  if (dialog == NULL || dialog->focus_guard_ollama_status_label == NULL) {
    return;
  }

  gtk_label_set_text(GTK_LABEL(dialog->focus_guard_ollama_status_label),
                     text != NULL ? text : "");
  gtk_widget_set_visible(dialog->focus_guard_ollama_status_label,
                         text != NULL && *text != '\0');
}

static void
focus_guard_model_weak_notify(gpointer data, GObject *where_the_object_was)
{
  (void)where_the_object_was;
  TimerSettingsDialog *dialog = data;
  if (dialog == NULL) {
    return;
  }

  g_message("focus_guard_model_weak_notify: dialog=%p model finalized", (void *)dialog);
  dialog->focus_guard_ollama_models = NULL;
  dialog->focus_guard_ollama_models_weak_set = FALSE;
}

static void
focus_guard_set_model_ref(TimerSettingsDialog *dialog, GtkStringList *model)
{
  if (dialog == NULL) {
    return;
  }

  if (dialog->focus_guard_ollama_models == model) {
    return;
  }

  if (dialog->focus_guard_ollama_models != NULL &&
      dialog->focus_guard_ollama_models_weak_set) {
    g_message("focus_guard_set_model_ref: remove weak ref dialog=%p model=%p",
              (void *)dialog,
              (void *)dialog->focus_guard_ollama_models);
    g_object_weak_unref(G_OBJECT(dialog->focus_guard_ollama_models),
                        focus_guard_model_weak_notify,
                        dialog);
    dialog->focus_guard_ollama_models_weak_set = FALSE;
  }

  g_set_object(&dialog->focus_guard_ollama_models, model);
  if (dialog->focus_guard_ollama_models != NULL) {
    g_message("focus_guard_set_model_ref: add weak ref dialog=%p model=%p",
              (void *)dialog,
              (void *)dialog->focus_guard_ollama_models);
    g_object_weak_ref(G_OBJECT(dialog->focus_guard_ollama_models),
                      focus_guard_model_weak_notify,
                      dialog);
    dialog->focus_guard_ollama_models_weak_set = TRUE;
  }
}

void
focus_guard_clear_model_ref(TimerSettingsDialog *dialog)
{
  if (dialog == NULL || dialog->focus_guard_ollama_models == NULL) {
    return;
  }

  if (dialog->focus_guard_ollama_models_weak_set) {
    g_message("focus_guard_clear_model_ref: remove weak ref dialog=%p model=%p",
              (void *)dialog,
              (void *)dialog->focus_guard_ollama_models);
    g_object_weak_unref(G_OBJECT(dialog->focus_guard_ollama_models),
                        focus_guard_model_weak_notify,
                        dialog);
    dialog->focus_guard_ollama_models_weak_set = FALSE;
  }

  g_message("focus_guard_clear_model_ref: unref model %p",
            (void *)dialog->focus_guard_ollama_models);
  g_clear_object(&dialog->focus_guard_ollama_models);
}

static GtkStringList *
focus_guard_get_model_list(TimerSettingsDialog *dialog)
{
  if (dialog == NULL || dialog->focus_guard_ollama_dropdown == NULL) {
    g_message("focus_guard_get_model_list: dialog=%p dropdown=%p (missing)",
              (void *)dialog,
              dialog != NULL ? (void *)dialog->focus_guard_ollama_dropdown : NULL);
    return NULL;
  }

  GListModel *model = gtk_drop_down_get_model(dialog->focus_guard_ollama_dropdown);
  g_message("focus_guard_get_model_list: dialog=%p dropdown=%p model=%p",
            (void *)dialog,
            (void *)dialog->focus_guard_ollama_dropdown,
            (void *)model);
  if (model == NULL || !GTK_IS_STRING_LIST(model)) {
    g_message("focus_guard_get_model_list: model invalid (is_string_list=%d)",
              model != NULL && GTK_IS_STRING_LIST(model));
    return NULL;
  }

  if (dialog->focus_guard_ollama_models == NULL) {
    g_message("focus_guard_get_model_list: cache model %p", (void *)model);
    focus_guard_set_model_ref(dialog, GTK_STRING_LIST(model));
  } else if (G_OBJECT(dialog->focus_guard_ollama_models) != G_OBJECT(model)) {
    g_message("focus_guard_get_model_list: mismatch cached=%p current=%p",
              (void *)dialog->focus_guard_ollama_models,
              (void *)model);
  }

  return GTK_STRING_LIST(model);
}

static char *
focus_guard_get_selected_model(TimerSettingsDialog *dialog)
{
  GtkStringList *list = focus_guard_get_model_list(dialog);
  if (list == NULL) {
    return NULL;
  }

  guint selected = gtk_drop_down_get_selected(dialog->focus_guard_ollama_dropdown);
  g_message("focus_guard_get_selected_model: dialog=%p dropdown=%p list=%p selected=%u",
            (void *)dialog,
            (void *)dialog->focus_guard_ollama_dropdown,
            (void *)list,
            selected);
  if (selected == GTK_INVALID_LIST_POSITION) {
    return NULL;
  }

  const char *value = gtk_string_list_get_string(list, selected);
  if (value == NULL || *value == '\0') {
    return NULL;
  }

  return g_strdup(value);
}

static void
focus_guard_update_ollama_toggle(TimerSettingsDialog *dialog)
{
  if (dialog == NULL || dialog->focus_guard_chrome_check == NULL) {
    return;
  }

  gboolean has_model = FALSE;
  if (focus_guard_get_model_list(dialog) != NULL) {
    guint selected = gtk_drop_down_get_selected(dialog->focus_guard_ollama_dropdown);
    has_model = selected != GTK_INVALID_LIST_POSITION;
  }

  g_message("focus_guard_update_ollama_toggle: dialog=%p has_model=%d",
            (void *)dialog,
            has_model);
  gtk_widget_set_sensitive(GTK_WIDGET(dialog->focus_guard_chrome_check), has_model);

  if (!has_model) {
    gtk_check_button_set_active(dialog->focus_guard_chrome_check, FALSE);
  }
}

static void
focus_guard_apply_model_selection(TimerSettingsDialog *dialog,
                                  const FocusGuardConfig *config)
{
  GtkStringList *list = focus_guard_get_model_list(dialog);
  if (dialog == NULL || dialog->focus_guard_ollama_dropdown == NULL ||
      list == NULL) {
    return;
  }

  guint selected = GTK_INVALID_LIST_POSITION;
  if (config != NULL && config->ollama_model != NULL) {
    guint count = g_list_model_get_n_items(G_LIST_MODEL(list));
    g_message("focus_guard_apply_model_selection: dialog=%p list=%p count=%u target=%s",
              (void *)dialog,
              (void *)list,
              count,
              config->ollama_model);
    for (guint i = 0; i < count; i++) {
      const char *item = gtk_string_list_get_string(list, i);
      if (item != NULL && g_strcmp0(item, config->ollama_model) == 0) {
        selected = i;
        break;
      }
    }
  }

  g_message("focus_guard_apply_model_selection: dialog=%p selected=%u",
            (void *)dialog,
            selected);
  gtk_drop_down_set_selected(dialog->focus_guard_ollama_dropdown, selected);
  focus_guard_update_ollama_toggle(dialog);
}

static void
focus_guard_apply_models_to_dropdown(TimerSettingsDialog *dialog,
                                     GPtrArray *models)
{
  if (dialog == NULL || dialog->focus_guard_ollama_dropdown == NULL) {
    return;
  }

  gboolean prev_suppress = dialog->suppress_signals;
  dialog->suppress_signals = TRUE;

  GtkStringList *list = focus_guard_get_model_list(dialog);
  if (list == NULL) {
    g_message("focus_guard_apply_models_to_dropdown: creating new list");
    list = gtk_string_list_new(NULL);
    gtk_drop_down_set_model(dialog->focus_guard_ollama_dropdown,
                            G_LIST_MODEL(list));
    focus_guard_set_model_ref(dialog, list);
    g_object_unref(list);
  }

  guint existing = g_list_model_get_n_items(G_LIST_MODEL(list));
  g_message("focus_guard_apply_models_to_dropdown: dialog=%p list=%p existing=%u new=%u",
            (void *)dialog,
            (void *)list,
            existing,
            models != NULL ? models->len : 0);
  if (existing > 0) {
    gtk_string_list_splice(list, 0, existing, NULL);
  }

  if (models != NULL) {
    for (guint i = 0; i < models->len; i++) {
      const char *model = g_ptr_array_index(models, i);
      if (model != NULL && *model != '\0') {
        gtk_string_list_append(list, model);
      }
    }
  }

  gtk_drop_down_set_selected(dialog->focus_guard_ollama_dropdown,
                             GTK_INVALID_LIST_POSITION);
  dialog->suppress_signals = prev_suppress;
}

static void
focus_guard_ollama_refresh_context_free(gpointer data)
{
  FocusGuardOllamaRefreshContext *context = data;
  if (context == NULL) {
    return;
  }

  g_weak_ref_clear(&context->window_ref);
  g_free(context);
}

static void
focus_guard_ollama_refresh_task(GTask *task,
                                gpointer source_object,
                                gpointer task_data,
                                GCancellable *cancellable)
{
  (void)source_object;
  (void)cancellable;
  (void)task_data;

  GError *error = NULL;
  GPtrArray *models = ollama_client_list_models_sync(&error);
  if (models == NULL) {
    g_task_return_error(task, error);
    return;
  }

  g_task_return_pointer(task, models, (GDestroyNotify)g_ptr_array_unref);
}

static void
focus_guard_ollama_refresh_complete(GObject *source_object,
                                    GAsyncResult *res,
                                    gpointer user_data)
{
  (void)source_object;
  FocusGuardOllamaRefreshContext *context = user_data;
  if (context == NULL) {
    return;
  }

  GtkWindow *window = g_weak_ref_get(&context->window_ref);
  g_message("focus_guard_ollama_refresh_complete: window=%p", (void *)window);
  if (window == NULL) {
    focus_guard_ollama_refresh_context_free(context);
    return;
  }

  TimerSettingsDialog *dialog =
      g_object_get_data(G_OBJECT(window), "timer-settings-dialog");
  g_message("focus_guard_ollama_refresh_complete: dialog=%p", (void *)dialog);
  if (dialog == NULL) {
    g_object_unref(window);
    focus_guard_ollama_refresh_context_free(context);
    return;
  }

  g_clear_object(&dialog->focus_guard_ollama_refresh_cancellable);
  if (dialog->focus_guard_ollama_refresh_button != NULL) {
    gtk_widget_set_sensitive(GTK_WIDGET(dialog->focus_guard_ollama_refresh_button), TRUE);
  }

  GError *error = NULL;
  GPtrArray *models = g_task_propagate_pointer(G_TASK(res), &error);
  g_message("focus_guard_ollama_refresh_complete: models=%p len=%u error=%s",
            (void *)models,
            models != NULL ? models->len : 0,
            error != NULL ? error->message : "(none)");
  if (models == NULL) {
    focus_guard_set_ollama_status(dialog,
                                  error != NULL && error->message != NULL
                                      ? error->message
                                      : "Unable to load Ollama models.");
    g_clear_error(&error);
    focus_guard_apply_models_to_dropdown(dialog, NULL);
  } else {
    focus_guard_apply_models_to_dropdown(dialog, models);
    if (models->len == 0) {
      focus_guard_set_ollama_status(
          dialog,
          "No Ollama models found. Use `ollama pull` to download one.");
    } else {
      focus_guard_set_ollama_status(dialog, NULL);
    }
    g_ptr_array_unref(models);
  }

  gboolean prev_suppress = dialog->suppress_signals;
  dialog->suppress_signals = TRUE;
  g_message("focus_guard_ollama_refresh_complete: update controls dialog=%p",
            (void *)dialog);
  focus_guard_settings_update_controls(dialog);
  dialog->suppress_signals = prev_suppress;
  focus_guard_apply_settings(dialog);

  g_object_unref(window);
  focus_guard_ollama_refresh_context_free(context);
}

static void
focus_guard_refresh_models(TimerSettingsDialog *dialog)
{
  if (dialog == NULL || dialog->focus_guard_ollama_dropdown == NULL) {
    g_message("focus_guard_refresh_models: dialog=%p dropdown=%p (skip)",
              (void *)dialog,
              dialog != NULL ? (void *)dialog->focus_guard_ollama_dropdown : NULL);
    return;
  }

  g_message("focus_guard_refresh_models: dialog=%p dropdown=%p",
            (void *)dialog,
            (void *)dialog->focus_guard_ollama_dropdown);
  if (dialog->focus_guard_ollama_refresh_cancellable != NULL) {
    g_cancellable_cancel(dialog->focus_guard_ollama_refresh_cancellable);
    g_clear_object(&dialog->focus_guard_ollama_refresh_cancellable);
  }

  if (dialog->focus_guard_ollama_refresh_button != NULL) {
    gtk_widget_set_sensitive(GTK_WIDGET(dialog->focus_guard_ollama_refresh_button), FALSE);
  }
  focus_guard_set_ollama_status(dialog, "Refreshing Ollama models...");

  dialog->focus_guard_ollama_refresh_cancellable = g_cancellable_new();

  FocusGuardOllamaRefreshContext *context =
      g_new0(FocusGuardOllamaRefreshContext, 1);
  g_weak_ref_init(&context->window_ref, G_OBJECT(dialog->window));

  GTask *task = g_task_new(NULL,
                           dialog->focus_guard_ollama_refresh_cancellable,
                           focus_guard_ollama_refresh_complete,
                           context);
  g_task_run_in_thread(task, focus_guard_ollama_refresh_task);
  g_object_unref(task);
}

static void
focus_guard_apply_settings(TimerSettingsDialog *dialog)
{
  if (dialog == NULL || dialog->suppress_signals || dialog->state == NULL ||
      dialog->state->focus_guard == NULL) {
    return;
  }

  g_message("focus_guard_apply_settings: dialog=%p state=%p",
            (void *)dialog,
            (void *)dialog->state);
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

  if (dialog->focus_guard_chrome_port_spin != NULL) {
    config.chrome_debug_port =
        (guint)gtk_spin_button_get_value_as_int(dialog->focus_guard_chrome_port_spin);
  }

  if (dialog->focus_guard_chrome_check != NULL) {
    config.chrome_ollama_enabled =
        gtk_check_button_get_active(dialog->focus_guard_chrome_check);
  }

  g_free(config.ollama_model);
  config.ollama_model = focus_guard_get_selected_model(dialog);

  if (config.ollama_model == NULL || *config.ollama_model == '\0') {
    config.chrome_ollama_enabled = FALSE;
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

  g_message("focus_guard_settings_update_controls: dialog=%p state=%p",
            (void *)dialog,
            (void *)dialog->state);
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

  if (dialog->focus_guard_chrome_port_spin != NULL) {
    gtk_spin_button_set_value(dialog->focus_guard_chrome_port_spin,
                              (gdouble)config.chrome_debug_port);
  }

  if (dialog->focus_guard_ollama_dropdown != NULL) {
    focus_guard_apply_model_selection(dialog, &config);
  }

  if (dialog->focus_guard_chrome_check != NULL) {
    gtk_check_button_set_active(dialog->focus_guard_chrome_check,
                                config.chrome_ollama_enabled);
  }

  focus_guard_update_ollama_toggle(dialog);

  if (dialog->focus_guard_list != NULL) {
    focus_guard_clear_list(dialog->focus_guard_list);
    if (config.blacklist != NULL) {
      for (gsize i = 0; config.blacklist[i] != NULL; i++) {
        focus_guard_append_blacklist_row(dialog, config.blacklist[i]);
      }
    }
    focus_guard_update_empty_label(dialog);
  }

  if (dialog->focus_guard_ollama_dropdown != NULL &&
      focus_guard_get_model_list(dialog) == NULL &&
      dialog->focus_guard_ollama_refresh_cancellable == NULL) {
    focus_guard_refresh_models(dialog);
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
on_focus_guard_chrome_toggled(GtkCheckButton *button, gpointer user_data)
{
  (void)button;
  focus_guard_apply_settings((TimerSettingsDialog *)user_data);
}

static void
on_focus_guard_chrome_port_changed(GtkSpinButton *spin, gpointer user_data)
{
  (void)spin;
  focus_guard_apply_settings((TimerSettingsDialog *)user_data);
}

static void
on_focus_guard_model_changed(GObject *object,
                             GParamSpec *pspec,
                             gpointer user_data)
{
  (void)object;
  (void)pspec;
  TimerSettingsDialog *dialog = user_data;
  if (dialog == NULL) {
    return;
  }

  if (dialog->suppress_signals) {
    return;
  }

  focus_guard_update_ollama_toggle(dialog);
  focus_guard_apply_settings(dialog);
}

static void
on_focus_guard_ollama_refresh_clicked(GtkButton *button, gpointer user_data)
{
  (void)button;
  focus_guard_refresh_models((TimerSettingsDialog *)user_data);
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
focus_guard_settings_append(TimerSettingsDialog *dialog,
                            GtkWidget *focus_root,
                            GtkWidget *chrome_root)
{
  if (dialog == NULL || focus_root == NULL) {
    return;
  }

  GtkWidget *guard_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_add_css_class(guard_card, "card");

  GtkWidget *guard_title = gtk_label_new("Focus guard");
  gtk_widget_add_css_class(guard_title, "card-title");
  gtk_widget_set_halign(guard_title, GTK_ALIGN_START);

  GtkWidget *guard_desc = gtk_label_new(
      "Warn when blacklisted apps take focus during a running session.");
  gtk_widget_add_css_class(guard_desc, "task-meta");
  gtk_widget_set_halign(guard_desc, GTK_ALIGN_START);
  gtk_label_set_wrap(GTK_LABEL(guard_desc), TRUE);

  GtkWidget *guard_grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(guard_grid), 10);
  gtk_grid_set_column_spacing(GTK_GRID(guard_grid), 16);

  GtkWidget *guard_global_label = gtk_label_new("Global app usage stats");
  gtk_widget_add_css_class(guard_global_label, "setting-label");
  gtk_widget_set_halign(guard_global_label, GTK_ALIGN_START);
  gtk_widget_set_hexpand(guard_global_label, TRUE);
  GtkWidget *guard_global_check = gtk_check_button_new();
  gtk_widget_set_halign(guard_global_check, GTK_ALIGN_END);
  gtk_widget_set_tooltip_text(guard_global_check,
                              "Track app usage continuously while the app runs.");

  GtkWidget *guard_warning_label = gtk_label_new("Warnings");
  gtk_widget_add_css_class(guard_warning_label, "setting-label");
  gtk_widget_set_halign(guard_warning_label, GTK_ALIGN_START);
  gtk_widget_set_hexpand(guard_warning_label, TRUE);
  GtkWidget *guard_warning_check = gtk_check_button_new();
  gtk_widget_set_halign(guard_warning_check, GTK_ALIGN_END);

  GtkWidget *guard_interval_label = gtk_label_new("Check interval (sec)");
  gtk_widget_add_css_class(guard_interval_label, "setting-label");
  gtk_widget_set_halign(guard_interval_label, GTK_ALIGN_START);
  gtk_widget_set_hexpand(guard_interval_label, TRUE);
  GtkWidget *guard_interval_spin = gtk_spin_button_new_with_range(1, 60, 1);
  gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(guard_interval_spin), TRUE);
  gtk_widget_add_css_class(guard_interval_spin, "setting-spin");
  gtk_widget_set_halign(guard_interval_spin, GTK_ALIGN_END);

  gtk_grid_attach(GTK_GRID(guard_grid), guard_global_label, 0, 0, 1, 1);
  gtk_grid_attach(GTK_GRID(guard_grid), guard_global_check, 1, 0, 1, 1);
  gtk_grid_attach(GTK_GRID(guard_grid), guard_warning_label, 0, 1, 1, 1);
  gtk_grid_attach(GTK_GRID(guard_grid), guard_warning_check, 1, 1, 1, 1);
  gtk_grid_attach(GTK_GRID(guard_grid), guard_interval_label, 0, 2, 1, 1);
  gtk_grid_attach(GTK_GRID(guard_grid), guard_interval_spin, 1, 2, 1, 1);

  gtk_box_append(GTK_BOX(guard_card), guard_title);
  gtk_box_append(GTK_BOX(guard_card), guard_desc);
  gtk_box_append(GTK_BOX(guard_card), guard_grid);

  GtkWidget *blacklist_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_add_css_class(blacklist_card, "card");

  GtkWidget *blacklist_title = gtk_label_new("Blacklisted apps");
  gtk_widget_add_css_class(blacklist_title, "card-title");
  gtk_widget_set_halign(blacklist_title, GTK_ALIGN_START);

  GtkWidget *blacklist_desc = gtk_label_new(
      "Add distractions here to get warned during focus sessions.");
  gtk_widget_add_css_class(blacklist_desc, "task-meta");
  gtk_widget_set_halign(blacklist_desc, GTK_ALIGN_START);
  gtk_label_set_wrap(GTK_LABEL(blacklist_desc), TRUE);

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
      140);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(guard_scroller), guard_list);

  GtkWidget *guard_empty_label = gtk_label_new("No blacklisted apps yet.");
  gtk_widget_add_css_class(guard_empty_label, "focus-guard-empty");
  gtk_widget_set_halign(guard_empty_label, GTK_ALIGN_START);
  gtk_label_set_wrap(GTK_LABEL(guard_empty_label), TRUE);

  gtk_box_append(GTK_BOX(blacklist_card), blacklist_title);
  gtk_box_append(GTK_BOX(blacklist_card), blacklist_desc);
  gtk_box_append(GTK_BOX(blacklist_card), guard_entry_row);
  gtk_box_append(GTK_BOX(blacklist_card), guard_active_row);
  gtk_box_append(GTK_BOX(blacklist_card), guard_scroller);
  gtk_box_append(GTK_BOX(blacklist_card), guard_empty_label);

  gtk_box_append(GTK_BOX(focus_root), guard_card);
  gtk_box_append(GTK_BOX(focus_root), blacklist_card);

  gboolean ollama_available = dialog->state != NULL &&
                              dialog->state->focus_guard != NULL &&
                              focus_guard_is_ollama_available(dialog->state->focus_guard);

  GtkCheckButton *chrome_check = NULL;
  GtkSpinButton *chrome_port_spin = NULL;
  GtkDropDown *ollama_dropdown = NULL;
  GtkButton *ollama_refresh_button = NULL;
  GtkWidget *ollama_status_label = NULL;
  GtkWidget *trafilatura_status_label = NULL;

  dialog->focus_guard_ollama_section = NULL;

  if (ollama_available && chrome_root != NULL) {
    GtkWidget *chrome_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_add_css_class(chrome_card, "card");

    GtkWidget *chrome_title = gtk_label_new("Chrome relevance");
    gtk_widget_add_css_class(chrome_title, "card-title");
    gtk_widget_set_halign(chrome_title, GTK_ALIGN_START);

    GtkWidget *chrome_desc = gtk_label_new(
        "When Chrome is active during a focus session, check if the page matches the current task.");
    gtk_widget_add_css_class(chrome_desc, "task-meta");
    gtk_widget_set_halign(chrome_desc, GTK_ALIGN_START);
    gtk_label_set_wrap(GTK_LABEL(chrome_desc), TRUE);

    GtkWidget *chrome_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(chrome_grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(chrome_grid), 16);

    GtkWidget *chrome_enable_label = gtk_label_new("Enable relevance check");
    gtk_widget_add_css_class(chrome_enable_label, "setting-label");
    gtk_widget_set_halign(chrome_enable_label, GTK_ALIGN_START);
    gtk_widget_set_hexpand(chrome_enable_label, TRUE);
    GtkWidget *chrome_enable_check = gtk_check_button_new();
    gtk_widget_set_halign(chrome_enable_check, GTK_ALIGN_END);

    GtkWidget *model_label = gtk_label_new("Ollama model");
    gtk_widget_add_css_class(model_label, "setting-label");
    gtk_widget_set_halign(model_label, GTK_ALIGN_START);
    gtk_widget_set_hexpand(model_label, TRUE);

    GtkStringList *model_list = gtk_string_list_new(NULL);
    GtkWidget *model_dropdown = gtk_drop_down_new(G_LIST_MODEL(model_list), NULL);
    gtk_widget_add_css_class(model_dropdown, "setting-dropdown");
    gtk_widget_set_hexpand(model_dropdown, TRUE);

    GtkWidget *model_refresh = gtk_button_new();
    gtk_widget_add_css_class(model_refresh, "icon-button");
    GtkWidget *refresh_icon = gtk_image_new_from_icon_name("view-refresh-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(refresh_icon), 18);
    gtk_button_set_child(GTK_BUTTON(model_refresh), refresh_icon);
    gtk_widget_set_tooltip_text(model_refresh, "Refresh models");

    GtkWidget *model_controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_hexpand(model_controls, TRUE);
    gtk_box_append(GTK_BOX(model_controls), model_dropdown);
    gtk_box_append(GTK_BOX(model_controls), model_refresh);

    GtkWidget *port_label = gtk_label_new("Chrome debug port");
    gtk_widget_add_css_class(port_label, "setting-label");
    gtk_widget_set_halign(port_label, GTK_ALIGN_START);
    gtk_widget_set_hexpand(port_label, TRUE);
    GtkWidget *port_spin = gtk_spin_button_new_with_range(1, 65535, 1);
    gtk_widget_add_css_class(port_spin, "setting-spin");
    gtk_widget_set_halign(port_spin, GTK_ALIGN_END);
    gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(port_spin), TRUE);

    gtk_grid_attach(GTK_GRID(chrome_grid), chrome_enable_label, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(chrome_grid), chrome_enable_check, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(chrome_grid), model_label, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(chrome_grid), model_controls, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(chrome_grid), port_label, 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(chrome_grid), port_spin, 1, 2, 1, 1);

    GtkWidget *chrome_hint = gtk_label_new(
        "Chrome must be started with --remote-debugging-port to enable page checks.");
    gtk_widget_add_css_class(chrome_hint, "task-meta");
    gtk_widget_set_halign(chrome_hint, GTK_ALIGN_START);
    gtk_label_set_wrap(GTK_LABEL(chrome_hint), TRUE);

    const char *trafilatura_text = NULL;
    TrafilaturaStatus trafilatura_status = trafilatura_client_get_status();
    switch (trafilatura_status) {
      case TRAFILATURA_STATUS_AVAILABLE:
        trafilatura_text = "Trafilatura enabled";
        break;
      case TRAFILATURA_STATUS_NO_PYTHON:
        trafilatura_text = "Trafilatura not available: python not found";
        break;
      case TRAFILATURA_STATUS_NO_MODULE:
      default:
        trafilatura_text = "Trafilatura not available: trafilatura not found";
        break;
    }

    GtkWidget *trafilatura_label = gtk_label_new(trafilatura_text);
    gtk_widget_add_css_class(trafilatura_label, "task-meta");
    gtk_widget_set_halign(trafilatura_label, GTK_ALIGN_START);
    gtk_label_set_wrap(GTK_LABEL(trafilatura_label), TRUE);

    GtkWidget *status_label = gtk_label_new("");
    gtk_widget_add_css_class(status_label, "task-meta");
    gtk_widget_set_halign(status_label, GTK_ALIGN_START);
    gtk_label_set_wrap(GTK_LABEL(status_label), TRUE);
    gtk_widget_set_visible(status_label, FALSE);

    gtk_box_append(GTK_BOX(chrome_card), chrome_title);
    gtk_box_append(GTK_BOX(chrome_card), chrome_desc);
    gtk_box_append(GTK_BOX(chrome_card), chrome_grid);
    gtk_box_append(GTK_BOX(chrome_card), chrome_hint);
    gtk_box_append(GTK_BOX(chrome_card), trafilatura_label);
    gtk_box_append(GTK_BOX(chrome_card), status_label);
    gtk_box_append(GTK_BOX(chrome_root), chrome_card);

    chrome_check = GTK_CHECK_BUTTON(chrome_enable_check);
    chrome_port_spin = GTK_SPIN_BUTTON(port_spin);
    ollama_dropdown = GTK_DROP_DOWN(model_dropdown);
    ollama_refresh_button = GTK_BUTTON(model_refresh);
    ollama_status_label = status_label;
    trafilatura_status_label = trafilatura_label;
    dialog->focus_guard_ollama_section = chrome_root;
    focus_guard_set_model_ref(dialog, model_list);
    g_object_unref(model_list);
  }

  dialog->focus_guard_global_check = GTK_CHECK_BUTTON(guard_global_check);
  dialog->focus_guard_warnings_check = GTK_CHECK_BUTTON(guard_warning_check);
  dialog->focus_guard_interval_spin = GTK_SPIN_BUTTON(guard_interval_spin);
  dialog->focus_guard_list = guard_list;
  dialog->focus_guard_empty_label = guard_empty_label;
  dialog->focus_guard_entry = guard_entry;
  dialog->focus_guard_active_label = guard_active_label;
  dialog->focus_guard_chrome_check = chrome_check;
  dialog->focus_guard_chrome_port_spin = chrome_port_spin;
  dialog->focus_guard_ollama_dropdown = ollama_dropdown;
  dialog->focus_guard_ollama_refresh_button = ollama_refresh_button;
  dialog->focus_guard_ollama_status_label = ollama_status_label;
  dialog->focus_guard_trafilatura_status_label = trafilatura_status_label;

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

  if (chrome_check != NULL) {
    g_signal_connect(chrome_check,
                     "toggled",
                     G_CALLBACK(on_focus_guard_chrome_toggled),
                     dialog);
  }
  if (chrome_port_spin != NULL) {
    g_signal_connect(chrome_port_spin,
                     "value-changed",
                     G_CALLBACK(on_focus_guard_chrome_port_changed),
                     dialog);
  }
  if (ollama_dropdown != NULL) {
    g_signal_connect(ollama_dropdown,
                     "notify::selected",
                     G_CALLBACK(on_focus_guard_model_changed),
                     dialog);
  }
  if (ollama_refresh_button != NULL) {
    g_signal_connect(ollama_refresh_button,
                     "clicked",
                     G_CALLBACK(on_focus_guard_ollama_refresh_clicked),
                     dialog);
  }

  if (ollama_available && dialog->focus_guard_ollama_section != NULL) {
    focus_guard_refresh_models(dialog);
  }
}
