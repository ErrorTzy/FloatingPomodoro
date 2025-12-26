#include "storage/settings_storage.h"

#include <errno.h>

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

  GKeyFile *key_file = g_key_file_new();
  if (!g_key_file_load_from_file(key_file, path, G_KEY_FILE_NONE, error)) {
    g_key_file_free(key_file);
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
  char *dir = g_path_get_dirname(path);
  if (g_mkdir_with_parents(dir, 0755) != 0) {
    g_set_error(error,
                G_FILE_ERROR,
                g_file_error_from_errno(errno),
                "Failed to create data directory '%s'",
                dir);
    g_free(dir);
    g_free(path);
    return FALSE;
  }
  g_free(dir);

  GKeyFile *key_file = g_key_file_new();

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
