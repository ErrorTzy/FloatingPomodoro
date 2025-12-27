#pragma once

#include <glib.h>

typedef struct {
  gboolean global_stats_enabled;
  gboolean warnings_enabled;
  guint detection_interval_seconds;
  char **blacklist;
  gboolean chrome_ollama_enabled;
  guint chrome_debug_port;
  char *ollama_model;
} FocusGuardConfig;

FocusGuardConfig focus_guard_config_default(void);
void focus_guard_config_normalize(FocusGuardConfig *config);
FocusGuardConfig focus_guard_config_copy(const FocusGuardConfig *config);
void focus_guard_config_clear(FocusGuardConfig *config);
