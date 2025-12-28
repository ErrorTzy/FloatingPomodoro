#include "focus/focus_guard_config.h"

static void
focus_guard_config_normalize_blacklist(FocusGuardConfig *config)
{
  if (config == NULL) {
    return;
  }

  if (config->blacklist == NULL) {
    config->blacklist = g_new0(char *, 1);
    return;
  }

  GPtrArray *items = g_ptr_array_new_with_free_func(g_free);
  GHashTable *seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

  for (gsize i = 0; config->blacklist[i] != NULL; i++) {
    const char *value = config->blacklist[i];
    if (value == NULL) {
      continue;
    }

    char *trimmed = g_strstrip(g_strdup(value));
    if (*trimmed == '\0') {
      g_free(trimmed);
      continue;
    }

    char *key = g_ascii_strdown(trimmed, -1);
    if (g_hash_table_contains(seen, key)) {
      g_free(key);
      g_free(trimmed);
      continue;
    }

    g_hash_table_add(seen, key);
    g_ptr_array_add(items, trimmed);
  }

  g_ptr_array_add(items, NULL);

  g_strfreev(config->blacklist);
  config->blacklist = (char **)g_ptr_array_free(items, FALSE);

  g_hash_table_destroy(seen);
}

FocusGuardConfig
focus_guard_config_default(void)
{
  FocusGuardConfig config = {0};
  config.global_stats_enabled = TRUE;
  config.warnings_enabled = TRUE;
  config.detection_interval_seconds = 1;
  config.blacklist = g_new0(char *, 1);
  config.chrome_ollama_enabled = FALSE;
  config.chrome_debug_port = 9222;
  config.ollama_model = NULL;
  config.trafilatura_python_path = NULL;
  return config;
}

void
focus_guard_config_normalize(FocusGuardConfig *config)
{
  if (config == NULL) {
    return;
  }

  if (config->detection_interval_seconds < 1) {
    config->detection_interval_seconds = 1;
  }

  if (config->chrome_debug_port < 1 || config->chrome_debug_port > 65535) {
    config->chrome_debug_port = 9222;
  }

  if (config->ollama_model != NULL) {
    char *trimmed = g_strstrip(config->ollama_model);
    if (*trimmed == '\0') {
      g_free(config->ollama_model);
      config->ollama_model = NULL;
    }
  }

  if (config->trafilatura_python_path != NULL) {
    char *trimmed = g_strstrip(config->trafilatura_python_path);
    if (*trimmed == '\0') {
      g_free(config->trafilatura_python_path);
      config->trafilatura_python_path = NULL;
    }
  }

  focus_guard_config_normalize_blacklist(config);
}

FocusGuardConfig
focus_guard_config_copy(const FocusGuardConfig *config)
{
  FocusGuardConfig copy = focus_guard_config_default();
  if (config == NULL) {
    return copy;
  }

  copy.warnings_enabled = config->warnings_enabled;
  copy.global_stats_enabled = config->global_stats_enabled;
  copy.detection_interval_seconds = config->detection_interval_seconds;
  copy.chrome_ollama_enabled = config->chrome_ollama_enabled;
  copy.chrome_debug_port = config->chrome_debug_port;
  g_strfreev(copy.blacklist);
  copy.blacklist = config->blacklist ? g_strdupv(config->blacklist) : g_new0(char *, 1);
  g_free(copy.ollama_model);
  copy.ollama_model = config->ollama_model ? g_strdup(config->ollama_model) : NULL;
  g_free(copy.trafilatura_python_path);
  copy.trafilatura_python_path = config->trafilatura_python_path
                                     ? g_strdup(config->trafilatura_python_path)
                                     : NULL;
  focus_guard_config_normalize(&copy);
  return copy;
}

void
focus_guard_config_clear(FocusGuardConfig *config)
{
  if (config == NULL) {
    return;
  }

  g_strfreev(config->blacklist);
  config->blacklist = NULL;
  g_clear_pointer(&config->ollama_model, g_free);
  g_clear_pointer(&config->trafilatura_python_path, g_free);
}
