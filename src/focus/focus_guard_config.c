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
  g_strfreev(copy.blacklist);
  copy.blacklist = config->blacklist ? g_strdupv(config->blacklist) : g_new0(char *, 1);
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
}
