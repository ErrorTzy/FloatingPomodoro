#pragma once

#include <glib.h>

#include "core/pomodoro_timer.h"

typedef struct {
  gboolean close_to_tray;
} AppSettings;

char *settings_storage_get_path(void);
gboolean settings_storage_load_timer(PomodoroTimerConfig *config, GError **error);
gboolean settings_storage_save_timer(const PomodoroTimerConfig *config, GError **error);
gboolean settings_storage_load_app(AppSettings *settings, GError **error);
gboolean settings_storage_save_app(const AppSettings *settings, GError **error);
