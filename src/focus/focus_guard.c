#include "focus/focus_guard_internal.h"

#include "focus/ollama_client.h"

static void
focus_guard_restart_timer(FocusGuard *guard)
{
  if (guard == NULL) {
    return;
  }

  if (guard->tick_source_id != 0) {
    g_source_remove(guard->tick_source_id);
    guard->tick_source_id = 0;
  }

  guard->last_tick_us = 0;
  guard->last_tick_real_us = 0;
  guard->last_warning_check_us = 0;

  guint interval = guard->config.detection_interval_seconds;
  if (interval < 1) {
    interval = 1;
  }
  if (guard->config.global_stats_enabled) {
    interval = 1;
  }

  guard->tick_source_id = g_timeout_add_seconds_full(G_PRIORITY_LOW,
                                                     interval,
                                                     focus_guard_on_tick,
                                                     guard,
                                                     NULL);
  focus_guard_on_tick(guard);
}

FocusGuard *
focus_guard_create(AppState *state, FocusGuardConfig config)
{
  if (state == NULL) {
    focus_guard_config_clear(&config);
    return NULL;
  }

  FocusGuard *guard = g_new0(FocusGuard, 1);
  guard->state = state;
  guard->stats_store = usage_stats_store_new();
  guard->usage_global = focus_guard_usage_table_new();
  guard->usage_task_view = NULL;
  guard->bucket_global = focus_guard_usage_table_new();
  guard->bucket_task = focus_guard_bucket_task_table_new();
  guard->bucket_start_utc = 0;
  guard->day_start_utc = 0;
  guard->config = focus_guard_config_copy(&config);
  focus_guard_build_blacklist(guard);
  guard->ollama_available = ollama_client_detect_available();
  if (!guard->ollama_available ||
      guard->config.ollama_model == NULL ||
      *guard->config.ollama_model == '\0') {
    guard->config.chrome_ollama_enabled = FALSE;
  }
  guard->relevance_warning_active = FALSE;
  guard->relevance_warning_text = NULL;
  guard->relevance_state = FOCUS_GUARD_RELEVANCE_UNKNOWN;
  guard->last_relevance_check_us = 0;
  guard->relevance_check_id = 0;
  guard->relevance_inflight = FALSE;
  guard->relevance_cancellable = NULL;
  focus_guard_refresh_day(guard);
  focus_guard_prune_history(guard);
  if (guard->usage_global != NULL) {
    focus_guard_load_usage_map_from_db(guard, guard->usage_global, "global", NULL);
  }
  guard->view = FOCUS_GUARD_VIEW_GLOBAL;
  focus_guard_restart_timer(guard);
  return guard;
}

void
focus_guard_destroy(FocusGuard *guard)
{
  if (guard == NULL) {
    return;
  }

  if (guard->tick_source_id != 0) {
    g_source_remove(guard->tick_source_id);
    guard->tick_source_id = 0;
  }

  focus_guard_cancel_relevance_check(guard);
  g_clear_pointer(&guard->relevance_warning_text, g_free);

  focus_guard_flush_bucket(guard);

  if (guard->usage_global != NULL) {
    g_hash_table_destroy(guard->usage_global);
  }

  if (guard->usage_task_view != NULL) {
    g_hash_table_destroy(guard->usage_task_view);
  }

  if (guard->bucket_global != NULL) {
    g_hash_table_destroy(guard->bucket_global);
  }

  if (guard->bucket_task != NULL) {
    g_hash_table_destroy(guard->bucket_task);
  }

  usage_stats_store_free(guard->stats_store);

  g_strfreev(guard->blacklist_norm);
  focus_guard_config_clear(&guard->config);
  g_free(guard->warning_app);
  g_free(guard->view_task_id);
  g_free(guard->view_task_title);
  g_free(guard->day_label);
  g_free(guard);
}

void
focus_guard_apply_config(FocusGuard *guard, FocusGuardConfig config)
{
  if (guard == NULL) {
    focus_guard_config_clear(&config);
    return;
  }

  gboolean was_global_enabled = guard->config.global_stats_enabled;
  gboolean was_chrome_enabled = guard->config.chrome_ollama_enabled;
  char *previous_model =
      guard->config.ollama_model != NULL ? g_strdup(guard->config.ollama_model)
                                         : NULL;

  focus_guard_config_clear(&guard->config);
  guard->config = focus_guard_config_copy(&config);
  focus_guard_build_blacklist(guard);

  if (!guard->ollama_available ||
      guard->config.ollama_model == NULL ||
      *guard->config.ollama_model == '\0') {
    guard->config.chrome_ollama_enabled = FALSE;
  }

  if (was_global_enabled && !guard->config.global_stats_enabled) {
    focus_guard_flush_bucket(guard);
  }

  focus_guard_restart_timer(guard);

  if (!guard->config.warnings_enabled) {
    focus_guard_set_warning(guard, FALSE, NULL);
    focus_guard_clear_relevance_warning(guard);
    focus_guard_cancel_relevance_check(guard);
  }

  gboolean model_changed = g_strcmp0(previous_model, guard->config.ollama_model) != 0;
  if ((!guard->config.chrome_ollama_enabled && was_chrome_enabled) || model_changed) {
    focus_guard_clear_relevance_warning(guard);
    focus_guard_cancel_relevance_check(guard);
  }

  if (guard->config.global_stats_enabled && !was_global_enabled &&
      guard->usage_global != NULL) {
    focus_guard_load_usage_map_from_db(guard, guard->usage_global, "global", NULL);
  }

  g_free(previous_model);
  guard->usage_dirty = TRUE;
  focus_guard_update_stats_ui(guard);
}

FocusGuardConfig
focus_guard_get_config(const FocusGuard *guard)
{
  if (guard == NULL) {
    return focus_guard_config_default();
  }

  return focus_guard_config_copy(&guard->config);
}

gboolean
focus_guard_is_ollama_available(const FocusGuard *guard)
{
  return guard != NULL && guard->ollama_available;
}
