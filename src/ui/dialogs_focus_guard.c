#include "ui/dialogs_focus_guard_internal.h"

#include "focus/trafilatura_client.h"
#include "focus/focus_guard_x11.h"
#include "storage/settings_storage.h"

static void
focus_guard_update_trafilatura_status(TimerSettingsDialog *dialog,
                                      const FocusGuardConfig *config)
{
  if (dialog == NULL || dialog->focus_guard_trafilatura_status_label == NULL) {
    return;
  }

  const char *python_path = NULL;
  if (config != NULL && config->trafilatura_python_path != NULL &&
      *config->trafilatura_python_path != '\0') {
    python_path = config->trafilatura_python_path;
  }

  const char *status_text = NULL;
  TrafilaturaStatus status = trafilatura_client_get_status(python_path);
  switch (status) {
    case TRAFILATURA_STATUS_AVAILABLE:
      status_text = "Trafilatura enabled";
      break;
    case TRAFILATURA_STATUS_NO_PYTHON:
      status_text = "Trafilatura not available: python not found";
      break;
    case TRAFILATURA_STATUS_NO_MODULE:
    default:
      status_text = "Trafilatura not available: trafilatura not found";
      break;
  }

  char *label = NULL;
  if (python_path != NULL) {
    label = g_strdup_printf("%s (python: %s)", status_text, python_path);
  } else {
    label = g_strdup(status_text);
  }

  gtk_label_set_text(GTK_LABEL(dialog->focus_guard_trafilatura_status_label),
                     label);
  g_free(label);
}

void
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

  if (dialog->focus_guard_chrome_port_spin != NULL) {
    config.chrome_debug_port =
        (guint)gtk_spin_button_get_value_as_int(dialog->focus_guard_chrome_port_spin);
  }

  if (dialog->focus_guard_chrome_check != NULL) {
    config.chrome_ollama_enabled =
        gtk_check_button_get_active(dialog->focus_guard_chrome_check);
  }

  if (dialog->focus_guard_trafilatura_python_entry != NULL) {
    g_free(config.trafilatura_python_path);
    config.trafilatura_python_path = g_strdup(gtk_editable_get_text(
        GTK_EDITABLE(dialog->focus_guard_trafilatura_python_entry)));
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
  focus_guard_update_trafilatura_status(dialog, &config);

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

  const char *last_external = NULL;
  if (dialog->focus_guard_model != NULL) {
    last_external =
        focus_guard_settings_model_get_last_external(dialog->focus_guard_model);
  }

  char *app_name = NULL;
  if (focus_guard_x11_get_active_app(&app_name, NULL) && app_name != NULL) {
    gboolean is_self = focus_guard_is_self_app(app_name);
    if (!is_self) {
      if (dialog->focus_guard_model != NULL) {
        focus_guard_settings_model_set_last_external(dialog->focus_guard_model,
                                                     app_name);
        last_external = focus_guard_settings_model_get_last_external(
            dialog->focus_guard_model);
      }
    }

    if (!is_self) {
      char *text = g_strdup_printf("Active app: %s", app_name);
      gtk_label_set_text(GTK_LABEL(dialog->focus_guard_active_label), text);
      g_free(text);
    } else if (last_external != NULL) {
      char *text = g_strdup_printf("Last active app: %s",
                                   last_external);
      gtk_label_set_text(GTK_LABEL(dialog->focus_guard_active_label), text);
      g_free(text);
    } else {
      gtk_label_set_text(GTK_LABEL(dialog->focus_guard_active_label),
                         "Last active app: none yet");
    }
  } else if (last_external != NULL) {
    char *text = g_strdup_printf("Last active app: %s",
                                 last_external);
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

  if (dialog->focus_guard_chrome_port_spin != NULL) {
    gtk_spin_button_set_value(dialog->focus_guard_chrome_port_spin,
                              (gdouble)config.chrome_debug_port);
  }

  if (dialog->focus_guard_trafilatura_python_entry != NULL) {
    gtk_editable_set_text(
        GTK_EDITABLE(dialog->focus_guard_trafilatura_python_entry),
        config.trafilatura_python_path != NULL
            ? config.trafilatura_python_path
            : "");
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
      dialog->focus_guard_model != NULL) {
    GtkStringList *list = focus_guard_get_model_list(dialog);
    guint count = 0;
    if (list != NULL) {
      count = g_list_model_get_n_items(G_LIST_MODEL(list));
    }
    if (count == 0 &&
        focus_guard_settings_model_get_refresh_cancellable(
            dialog->focus_guard_model) == NULL) {
      focus_guard_refresh_models(dialog);
    }
  }

  focus_guard_update_trafilatura_status(dialog, &config);
  focus_guard_config_clear(&config);
}

void
on_focus_guard_interval_changed(GtkSpinButton *spin, gpointer user_data)
{
  (void)spin;
  focus_guard_apply_settings((TimerSettingsDialog *)user_data);
}

void
on_focus_guard_global_toggled(GtkCheckButton *button, gpointer user_data)
{
  (void)button;
  focus_guard_apply_settings((TimerSettingsDialog *)user_data);
}

void
on_focus_guard_warnings_toggled(GtkCheckButton *button, gpointer user_data)
{
  (void)button;
  focus_guard_apply_settings((TimerSettingsDialog *)user_data);
}

void
on_focus_guard_chrome_toggled(GtkCheckButton *button, gpointer user_data)
{
  (void)button;
  focus_guard_apply_settings((TimerSettingsDialog *)user_data);
}

void
on_focus_guard_chrome_port_changed(GtkSpinButton *spin, gpointer user_data)
{
  (void)spin;
  focus_guard_apply_settings((TimerSettingsDialog *)user_data);
}

void
on_focus_guard_trafilatura_python_changed(GtkEditable *editable,
                                          gpointer user_data)
{
  (void)editable;
  focus_guard_apply_settings((TimerSettingsDialog *)user_data);
}
