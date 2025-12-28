#include "ui/dialogs_focus_guard_internal.h"

#include "focus/ollama_client.h"

typedef struct {
  GWeakRef window_ref;
  GWeakRef model_ref;
} FocusGuardOllamaRefreshContext;

void
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

GtkStringList *
focus_guard_get_model_list(TimerSettingsDialog *dialog)
{
  if (dialog == NULL || dialog->focus_guard_model == NULL) {
    return NULL;
  }

  return focus_guard_settings_model_get_ollama_models(dialog->focus_guard_model);
}

GtkWidget *
focus_guard_create_model_dropdown(TimerSettingsDialog *dialog)
{
  GtkWidget *dropdown = gtk_drop_down_new(NULL, NULL);
  if (dialog == NULL) {
    return dropdown;
  }

  GtkStringList *list = focus_guard_get_model_list(dialog);
  if (list != NULL) {
    gtk_drop_down_set_model(GTK_DROP_DOWN(dropdown), G_LIST_MODEL(list));
    g_assert(gtk_drop_down_get_model(GTK_DROP_DOWN(dropdown)) ==
             G_LIST_MODEL(list));
  }

  return dropdown;
}

char *
focus_guard_get_selected_model(TimerSettingsDialog *dialog)
{
  GtkStringList *list = focus_guard_get_model_list(dialog);
  if (list == NULL) {
    return NULL;
  }

  guint selected = gtk_drop_down_get_selected(dialog->focus_guard_ollama_dropdown);
  if (selected == GTK_INVALID_LIST_POSITION) {
    return NULL;
  }

  const char *value = gtk_string_list_get_string(list, selected);
  if (value == NULL || *value == '\0') {
    return NULL;
  }

  return g_strdup(value);
}

void
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

  gtk_widget_set_sensitive(GTK_WIDGET(dialog->focus_guard_chrome_check), has_model);

  if (!has_model) {
    gtk_check_button_set_active(dialog->focus_guard_chrome_check, FALSE);
  }
}

void
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
    for (guint i = 0; i < count; i++) {
      const char *item = gtk_string_list_get_string(list, i);
      if (item != NULL && g_strcmp0(item, config->ollama_model) == 0) {
        selected = i;
        break;
      }
    }
  }

  gtk_drop_down_set_selected(dialog->focus_guard_ollama_dropdown, selected);
  focus_guard_update_ollama_toggle(dialog);
}

void
focus_guard_apply_models_to_dropdown(TimerSettingsDialog *dialog,
                                     GPtrArray *models)
{
  if (dialog == NULL || dialog->focus_guard_ollama_dropdown == NULL) {
    return;
  }

  gboolean prev_suppress = dialog->suppress_signals;
  dialog->suppress_signals = TRUE;

  if (dialog->focus_guard_model == NULL ||
      focus_guard_get_model_list(dialog) == NULL) {
    dialog->suppress_signals = prev_suppress;
    return;
  }

  focus_guard_settings_model_replace_ollama_models(dialog->focus_guard_model,
                                                   models);

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
  g_weak_ref_clear(&context->model_ref);
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

  FocusGuardSettingsModel *model =
      g_weak_ref_get(&context->model_ref);
  if (model == NULL) {
    focus_guard_ollama_refresh_context_free(context);
    return;
  }

  GtkWindow *window = g_weak_ref_get(&context->window_ref);
  TimerSettingsDialog *dialog = NULL;
  if (window != NULL) {
    dialog = g_object_get_data(G_OBJECT(window), "timer-settings-dialog");
  }

  focus_guard_settings_model_set_refresh_cancellable(model, NULL);
  if (dialog != NULL && dialog->focus_guard_ollama_refresh_button != NULL) {
    gtk_widget_set_sensitive(GTK_WIDGET(dialog->focus_guard_ollama_refresh_button),
                             TRUE);
  }

  GError *error = NULL;
  GPtrArray *models = g_task_propagate_pointer(G_TASK(res), &error);
  if (models == NULL) {
    if (dialog != NULL) {
      focus_guard_set_ollama_status(dialog,
                                    error != NULL && error->message != NULL
                                        ? error->message
                                        : "Unable to load Ollama models.");
    }
    g_clear_error(&error);
    if (dialog != NULL) {
      focus_guard_apply_models_to_dropdown(dialog, NULL);
    } else {
      focus_guard_settings_model_replace_ollama_models(model, NULL);
    }
  } else {
    if (dialog != NULL) {
      focus_guard_apply_models_to_dropdown(dialog, models);
    } else {
      focus_guard_settings_model_replace_ollama_models(model, models);
    }
    if (dialog != NULL) {
      if (models->len == 0) {
        focus_guard_set_ollama_status(
            dialog,
            "No Ollama models found. Use `ollama pull` to download one.");
      } else {
        focus_guard_set_ollama_status(dialog, NULL);
      }
    }
    g_ptr_array_unref(models);
  }

  if (dialog != NULL) {
    gboolean prev_suppress = dialog->suppress_signals;
    dialog->suppress_signals = TRUE;
    focus_guard_settings_update_controls(dialog);
    dialog->suppress_signals = prev_suppress;
    focus_guard_apply_settings(dialog);
  }

  g_clear_object(&model);
  if (window != NULL) {
    g_object_unref(window);
  }
  focus_guard_ollama_refresh_context_free(context);
}

void
focus_guard_refresh_models(TimerSettingsDialog *dialog)
{
  if (dialog == NULL || dialog->focus_guard_ollama_dropdown == NULL ||
      dialog->focus_guard_model == NULL) {
    return;
  }

  focus_guard_settings_model_cancel_refresh(dialog->focus_guard_model);

  if (dialog->focus_guard_ollama_refresh_button != NULL) {
    gtk_widget_set_sensitive(GTK_WIDGET(dialog->focus_guard_ollama_refresh_button),
                             FALSE);
  }
  focus_guard_set_ollama_status(dialog, "Refreshing Ollama models...");

  GCancellable *cancellable = g_cancellable_new();
  focus_guard_settings_model_set_refresh_cancellable(dialog->focus_guard_model,
                                                     cancellable);

  FocusGuardOllamaRefreshContext *context =
      g_new0(FocusGuardOllamaRefreshContext, 1);
  g_weak_ref_init(&context->window_ref, G_OBJECT(dialog->window));
  g_weak_ref_init(&context->model_ref, G_OBJECT(dialog->focus_guard_model));

  GTask *task = g_task_new(NULL,
                           cancellable,
                           focus_guard_ollama_refresh_complete,
                           context);
  g_task_run_in_thread(task, focus_guard_ollama_refresh_task);
  g_object_unref(task);
  g_object_unref(cancellable);
}

void
on_focus_guard_model_changed(GObject *object,
                             GParamSpec *pspec,
                             gpointer user_data)
{
  (void)object;
  (void)pspec;
  TimerSettingsDialog *dialog = user_data;
  if (dialog == NULL || dialog->suppress_signals || dialog->focus_guard_model == NULL ||
      dialog->focus_guard_ollama_dropdown == NULL) {
    return;
  }

  GtkStringList *list = focus_guard_get_model_list(dialog);
  if (list == NULL) {
    return;
  }

  guint selected = gtk_drop_down_get_selected(dialog->focus_guard_ollama_dropdown);
  if (selected != GTK_INVALID_LIST_POSITION) {
    const char *value = gtk_string_list_get_string(list, selected);
    focus_guard_settings_model_set_last_external(dialog->focus_guard_model, value);
  }

  focus_guard_update_ollama_toggle(dialog);
  focus_guard_apply_settings(dialog);
}

void
on_focus_guard_ollama_refresh_clicked(GtkButton *button, gpointer user_data)
{
  (void)button;
  focus_guard_refresh_models((TimerSettingsDialog *)user_data);
}
