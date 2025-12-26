#pragma once

#include <glib.h>

#include "core/pomodoro_timer.h"

char *settings_storage_get_path(void);
gboolean settings_storage_load_timer(PomodoroTimerConfig *config, GError **error);
gboolean settings_storage_save_timer(const PomodoroTimerConfig *config, GError **error);
