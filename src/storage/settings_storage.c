#include "storage/settings_storage.h"

#include <errno.h>

AppSettings
settings_storage_app_default(void)
{
  AppSettings settings = {0};
  settings.close_to_tray = TRUE;
  settings.autostart_enabled = FALSE;
  settings.autostart_start_in_tray = TRUE;
  settings.minimize_to_tray = FALSE;
  return settings;
}

static GKeyFile *
settings_storage_load_key_file(const char *path, GError **error)
{
  GKeyFile *key_file = g_key_file_new();

  if (g_file_test(path, G_FILE_TEST_EXISTS)) {
    if (!g_key_file_load_from_file(key_file, path, G_KEY_FILE_NONE, error)) {
      g_key_file_free(key_file);
      return NULL;
    }
  }

  return key_file;
}

static gboolean
settings_storage_ensure_dir(const char *path, GError **error)
{
  char *dir = g_path_get_dirname(path);
  if (g_mkdir_with_parents(dir, 0755) != 0) {
    g_set_error(error,
                G_FILE_ERROR,
                g_file_error_from_errno(errno),
                "Failed to create data directory '%s'",
                dir);
    g_free(dir);
    return FALSE;
  }
  g_free(dir);
  return TRUE;
}

char *
settings_storage_get_path(void)
{
  return g_build_filename(g_get_user_data_dir(),
                          "xfce4-floating-pomodoro",
                          "settings.ini",
                          NULL);
}

gboolean
settings_storage_load_timer(PomodoroTimerConfig *config, GError **error)
{
  if (config == NULL) {
    g_set_error(error,
                G_FILE_ERROR,
                G_FILE_ERROR_INVAL,
                "Timer config is NULL");
    return FALSE;
  }

  *config = pomodoro_timer_config_default();

  char *path = settings_storage_get_path();
  if (!g_file_test(path, G_FILE_TEST_EXISTS)) {
    g_free(path);
    return TRUE;
  }

  GKeyFile *key_file = settings_storage_load_key_file(path, error);
  if (key_file == NULL) {
    g_free(path);
    return FALSE;
  }

  if (g_key_file_has_key(key_file, "timer", "focus_minutes", NULL)) {
    gint value = g_key_file_get_integer(key_file,
                                        "timer",
                                        "focus_minutes",
                                        NULL);
    if (value > 0) {
      config->focus_minutes = (guint)value;
    }
  }
  if (g_key_file_has_key(key_file, "timer", "short_break_minutes", NULL)) {
    gint value = g_key_file_get_integer(key_file,
                                        "timer",
                                        "short_break_minutes",
                                        NULL);
    if (value > 0) {
      config->short_break_minutes = (guint)value;
    }
  }
  if (g_key_file_has_key(key_file, "timer", "long_break_minutes", NULL)) {
    gint value = g_key_file_get_integer(key_file,
                                        "timer",
                                        "long_break_minutes",
                                        NULL);
    if (value > 0) {
      config->long_break_minutes = (guint)value;
    }
  }
  if (g_key_file_has_key(key_file, "timer", "long_break_interval", NULL)) {
    gint value = g_key_file_get_integer(key_file,
                                        "timer",
                                        "long_break_interval",
                                        NULL);
    if (value > 0) {
      config->long_break_interval = (guint)value;
    }
  }

  *config = pomodoro_timer_config_normalize(*config);

  g_key_file_free(key_file);
  g_free(path);
  return TRUE;
}

gboolean
settings_storage_save_timer(const PomodoroTimerConfig *config, GError **error)
{
  if (config == NULL) {
    g_set_error(error,
                G_FILE_ERROR,
                G_FILE_ERROR_INVAL,
                "Timer config is NULL");
    return FALSE;
  }

  PomodoroTimerConfig normalized = pomodoro_timer_config_normalize(*config);

  char *path = settings_storage_get_path();
  if (!settings_storage_ensure_dir(path, error)) {
    g_free(path);
    return FALSE;
  }

  GKeyFile *key_file = settings_storage_load_key_file(path, error);
  if (key_file == NULL) {
    g_free(path);
    return FALSE;
  }

  g_key_file_set_integer(key_file,
                         "timer",
                         "focus_minutes",
                         (gint)normalized.focus_minutes);
  g_key_file_set_integer(key_file,
                         "timer",
                         "short_break_minutes",
                         (gint)normalized.short_break_minutes);
  g_key_file_set_integer(key_file,
                         "timer",
                         "long_break_minutes",
                         (gint)normalized.long_break_minutes);
  g_key_file_set_integer(key_file,
                         "timer",
                         "long_break_interval",
                         (gint)normalized.long_break_interval);

  gsize length = 0;
  gchar *data = g_key_file_to_data(key_file, &length, error);
  if (data == NULL) {
    g_key_file_free(key_file);
    g_free(path);
    return FALSE;
  }

  gboolean result = g_file_set_contents(path, data, length, error);

  g_free(data);
  g_key_file_free(key_file);
  g_free(path);
  return result;
}

gboolean
settings_storage_load_focus_guard(FocusGuardConfig *config, GError **error)
{
  if (config == NULL) {
    g_set_error(error,
                G_FILE_ERROR,
                G_FILE_ERROR_INVAL,
                "Focus guard config is NULL");
    return FALSE;
  }

  *config = focus_guard_config_default();

  char *path = settings_storage_get_path();
  if (!g_file_test(path, G_FILE_TEST_EXISTS)) {
    g_free(path);
    return TRUE;
  }

  GKeyFile *key_file = settings_storage_load_key_file(path, error);
  if (key_file == NULL) {
    g_free(path);
    return FALSE;
  }

  if (g_key_file_has_key(key_file, "focus_guard", "warnings_enabled", NULL)) {
    config->warnings_enabled =
        g_key_file_get_boolean(key_file,
                               "focus_guard",
                               "warnings_enabled",
                               NULL);
  }

  if (g_key_file_has_key(key_file, "focus_guard", "global_stats_enabled", NULL)) {
    config->global_stats_enabled =
        g_key_file_get_boolean(key_file,
                               "focus_guard",
                               "global_stats_enabled",
                               NULL);
  }

  if (g_key_file_has_key(key_file, "focus_guard", "interval_seconds", NULL)) {
    gint value = g_key_file_get_integer(key_file,
                                        "focus_guard",
                                        "interval_seconds",
                                        NULL);
    if (value > 0) {
      config->detection_interval_seconds = (guint)value;
    }
  }

  if (g_key_file_has_key(key_file, "focus_guard", "chrome_ollama_enabled", NULL)) {
    config->chrome_ollama_enabled =
        g_key_file_get_boolean(key_file,
                               "focus_guard",
                               "chrome_ollama_enabled",
                               NULL);
  }

  if (g_key_file_has_key(key_file, "focus_guard", "chrome_debug_port", NULL)) {
    gint value = g_key_file_get_integer(key_file,
                                        "focus_guard",
                                        "chrome_debug_port",
                                        NULL);
    if (value > 0) {
      config->chrome_debug_port = (guint)value;
    }
  }

  if (g_key_file_has_key(key_file, "focus_guard", "ollama_model", NULL)) {
    gchar *value = g_key_file_get_string(key_file,
                                         "focus_guard",
                                         "ollama_model",
                                         NULL);
    if (value != NULL) {
      g_free(config->ollama_model);
      config->ollama_model = g_strdup(value);
      g_free(value);
    }
  }

  if (g_key_file_has_key(key_file, "focus_guard", "blacklist", NULL)) {
    gsize length = 0;
    gchar **list = g_key_file_get_string_list(key_file,
                                              "focus_guard",
                                              "blacklist",
                                              &length,
                                              NULL);
    if (list != NULL) {
      g_strfreev(config->blacklist);
      config->blacklist = list;
    }
  }

  focus_guard_config_normalize(config);

  g_key_file_free(key_file);
  g_free(path);
  return TRUE;
}

gboolean
settings_storage_save_focus_guard(const FocusGuardConfig *config,
                                  GError **error)
{
  if (config == NULL) {
    g_set_error(error,
                G_FILE_ERROR,
                G_FILE_ERROR_INVAL,
                "Focus guard config is NULL");
    return FALSE;
  }

  FocusGuardConfig normalized = focus_guard_config_copy(config);
  focus_guard_config_normalize(&normalized);

  char *path = settings_storage_get_path();
  if (!settings_storage_ensure_dir(path, error)) {
    g_free(path);
    focus_guard_config_clear(&normalized);
    return FALSE;
  }

  GKeyFile *key_file = settings_storage_load_key_file(path, error);
  if (key_file == NULL) {
    g_free(path);
    focus_guard_config_clear(&normalized);
    return FALSE;
  }

  g_key_file_set_boolean(key_file,
                         "focus_guard",
                         "warnings_enabled",
                         normalized.warnings_enabled);
  g_key_file_set_boolean(key_file,
                         "focus_guard",
                         "global_stats_enabled",
                         normalized.global_stats_enabled);
  g_key_file_set_integer(key_file,
                         "focus_guard",
                         "interval_seconds",
                         (gint)normalized.detection_interval_seconds);
  g_key_file_set_boolean(key_file,
                         "focus_guard",
                         "chrome_ollama_enabled",
                         normalized.chrome_ollama_enabled);
  g_key_file_set_integer(key_file,
                         "focus_guard",
                         "chrome_debug_port",
                         (gint)normalized.chrome_debug_port);
  g_key_file_set_string(key_file,
                        "focus_guard",
                        "ollama_model",
                        normalized.ollama_model != NULL
                            ? normalized.ollama_model
                            : "");

  if (normalized.blacklist != NULL) {
    gsize length = g_strv_length(normalized.blacklist);
    g_key_file_set_string_list(key_file,
                               "focus_guard",
                               "blacklist",
                               (const gchar *const *)normalized.blacklist,
                               length);
  } else {
    g_key_file_set_string_list(key_file,
                               "focus_guard",
                               "blacklist",
                               NULL,
                               0);
  }

  gsize length = 0;
  gchar *data = g_key_file_to_data(key_file, &length, error);
  if (data == NULL) {
    g_key_file_free(key_file);
    g_free(path);
    focus_guard_config_clear(&normalized);
    return FALSE;
  }

  gboolean result = g_file_set_contents(path, data, length, error);

  g_free(data);
  g_key_file_free(key_file);
  g_free(path);
  focus_guard_config_clear(&normalized);
  return result;
}

gboolean
settings_storage_load_app(AppSettings *settings, GError **error)
{
  if (settings == NULL) {
    g_set_error(error,
                G_FILE_ERROR,
                G_FILE_ERROR_INVAL,
                "App settings is NULL");
    return FALSE;
  }

  *settings = settings_storage_app_default();

  char *path = settings_storage_get_path();
  if (!g_file_test(path, G_FILE_TEST_EXISTS)) {
    g_free(path);
    return TRUE;
  }

  GKeyFile *key_file = settings_storage_load_key_file(path, error);
  if (key_file == NULL) {
    g_free(path);
    return FALSE;
  }

  if (g_key_file_has_key(key_file, "app", "close_to_tray", NULL)) {
    settings->close_to_tray =
        g_key_file_get_boolean(key_file, "app", "close_to_tray", NULL);
  }
  if (g_key_file_has_key(key_file, "app", "autostart_enabled", NULL)) {
    settings->autostart_enabled =
        g_key_file_get_boolean(key_file, "app", "autostart_enabled", NULL);
  }
  if (g_key_file_has_key(key_file, "app", "autostart_start_in_tray", NULL)) {
    settings->autostart_start_in_tray =
        g_key_file_get_boolean(key_file,
                               "app",
                               "autostart_start_in_tray",
                               NULL);
  }
  if (g_key_file_has_key(key_file, "app", "minimize_to_tray", NULL)) {
    settings->minimize_to_tray =
        g_key_file_get_boolean(key_file, "app", "minimize_to_tray", NULL);
  }

  g_key_file_free(key_file);
  g_free(path);
  return TRUE;
}

gboolean
settings_storage_save_app(const AppSettings *settings, GError **error)
{
  if (settings == NULL) {
    g_set_error(error,
                G_FILE_ERROR,
                G_FILE_ERROR_INVAL,
                "App settings is NULL");
    return FALSE;
  }

  char *path = settings_storage_get_path();
  if (!settings_storage_ensure_dir(path, error)) {
    g_free(path);
    return FALSE;
  }

  GKeyFile *key_file = settings_storage_load_key_file(path, error);
  if (key_file == NULL) {
    g_free(path);
    return FALSE;
  }

  g_key_file_set_boolean(key_file,
                         "app",
                         "close_to_tray",
                         settings->close_to_tray);
  g_key_file_set_boolean(key_file,
                         "app",
                         "autostart_enabled",
                         settings->autostart_enabled);
  g_key_file_set_boolean(key_file,
                         "app",
                         "autostart_start_in_tray",
                         settings->autostart_start_in_tray);
  g_key_file_set_boolean(key_file,
                         "app",
                         "minimize_to_tray",
                         settings->minimize_to_tray);

  gsize length = 0;
  gchar *data = g_key_file_to_data(key_file, &length, error);
  if (data == NULL) {
    g_key_file_free(key_file);
    g_free(path);
    return FALSE;
  }

  gboolean result = g_file_set_contents(path, data, length, error);

  g_free(data);
  g_key_file_free(key_file);
  g_free(path);
  return result;
}
