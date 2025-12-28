#include "focus/focus_guard_internal.h"

#include "core/task_store.h"
#include "focus/focus_guard_x11.h"

#define USAGE_BUCKET_SECONDS 300
#define CHROME_RELEVANCE_INTERVAL_SECONDS 15

static void
focus_guard_clear_usage_table(GHashTable *table)
{
  if (table == NULL) {
    return;
  }

  g_hash_table_remove_all(table);
}

static FocusGuardBucketTaskEntry *
focus_guard_bucket_task_get_or_create(FocusGuard *guard,
                                      const char *task_id,
                                      const char *app_key,
                                      const char *app_name)
{
  if (guard == NULL || guard->bucket_task == NULL || task_id == NULL ||
      app_key == NULL) {
    return NULL;
  }

  char *key = g_strdup_printf("%s|%s", task_id, app_key);
  FocusGuardBucketTaskEntry *entry = g_hash_table_lookup(guard->bucket_task, key);
  if (entry != NULL) {
    g_free(key);
    if (app_name != NULL && g_strcmp0(entry->app_name, app_name) != 0) {
      g_free(entry->app_name);
      entry->app_name = g_strdup(app_name);
    }
    return entry;
  }

  entry = g_new0(FocusGuardBucketTaskEntry, 1);
  entry->task_id = g_strdup(task_id);
  entry->app_key = g_strdup(app_key);
  entry->app_name = g_strdup(app_name ? app_name : app_key);
  entry->usec_total = 0;
  g_hash_table_insert(guard->bucket_task, key, entry);
  return entry;
}

static void
focus_guard_rotate_bucket(FocusGuard *guard, gint64 now_utc_sec)
{
  if (guard == NULL) {
    return;
  }

  if (now_utc_sec < 0) {
    now_utc_sec = 0;
  }

  gint64 bucket_start =
      (now_utc_sec / USAGE_BUCKET_SECONDS) * USAGE_BUCKET_SECONDS;

  if (guard->bucket_start_utc == 0) {
    guard->bucket_start_utc = bucket_start;
    return;
  }

  if (bucket_start != guard->bucket_start_utc) {
    focus_guard_flush_bucket(guard);
    guard->bucket_start_utc = bucket_start;
  }
}

gboolean
focus_guard_on_tick(gpointer data)
{
  FocusGuard *guard = data;
  if (guard == NULL) {
    return G_SOURCE_CONTINUE;
  }

  gint64 now_us = g_get_monotonic_time();
  gint64 now_real_us = g_get_real_time();
  gint64 elapsed_us =
      guard->last_tick_real_us > 0 ? now_real_us - guard->last_tick_real_us : 0;
  guard->last_tick_real_us = now_real_us;
  guard->last_tick_us = now_us;

  if (elapsed_us < 0) {
    elapsed_us = 0;
  }

  guint interval = guard->config.detection_interval_seconds;
  if (interval < 1) {
    interval = 1;
  }

  gint64 max_elapsed = (gint64)interval * 3 * G_USEC_PER_SEC;
  if (max_elapsed > 0 && elapsed_us > max_elapsed) {
    elapsed_us = (gint64)interval * G_USEC_PER_SEC;
  }

  gboolean day_changed = focus_guard_refresh_day(guard);
  if (day_changed) {
    focus_guard_flush_bucket(guard);
    focus_guard_clear_usage_table(guard->usage_global);
    if (guard->usage_global != NULL) {
      focus_guard_load_usage_map_from_db(guard, guard->usage_global, "global", NULL);
    }
    if (guard->usage_task_view != NULL) {
      focus_guard_clear_usage_table(guard->usage_task_view);
      if (guard->view == FOCUS_GUARD_VIEW_TASK && guard->view_task_id != NULL) {
        focus_guard_load_usage_map_from_db(guard,
                                           guard->usage_task_view,
                                           "task",
                                           guard->view_task_id);
        focus_guard_merge_bucket_task(guard,
                                      guard->view_task_id,
                                      guard->usage_task_view);
      }
    }
    focus_guard_prune_history(guard);
    guard->usage_dirty = TRUE;
  }

  gint64 now_utc_sec = now_real_us / G_USEC_PER_SEC;
  focus_guard_rotate_bucket(guard, now_utc_sec);

  gboolean tracking = focus_guard_should_track(guard);
  PomodoroTask *active_task =
      tracking ? task_store_get_active(guard->state->store) : NULL;

  gboolean needs_app =
      guard->config.global_stats_enabled || tracking || guard->config.warnings_enabled;
  char *app_name = NULL;
  char *app_key = NULL;
  char *window_title = NULL;

  if (needs_app) {
    if (focus_guard_x11_get_active_app(&app_name, &window_title) && app_name != NULL) {
      app_key = g_ascii_strdown(app_name, -1);
    }
  }

  if (elapsed_us > 0 && app_key != NULL) {
    if (guard->config.global_stats_enabled && guard->usage_global != NULL) {
      FocusGuardUsage *usage =
          focus_guard_usage_get_or_create(guard->usage_global, app_key, app_name);
      if (usage != NULL) {
        usage->usec_total += elapsed_us;
        guard->usage_dirty |= (guard->view == FOCUS_GUARD_VIEW_GLOBAL);
      }

      if (guard->bucket_global != NULL) {
        FocusGuardUsage *bucket_usage =
            focus_guard_usage_get_or_create(guard->bucket_global, app_key, app_name);
        if (bucket_usage != NULL) {
          bucket_usage->usec_total += elapsed_us;
        }
      }
    }

    if (tracking && active_task != NULL) {
      const char *task_id = pomodoro_task_get_id(active_task);
      FocusGuardBucketTaskEntry *entry =
          focus_guard_bucket_task_get_or_create(guard, task_id, app_key, app_name);
      if (entry != NULL) {
        entry->usec_total += elapsed_us;
      }

      if (guard->view == FOCUS_GUARD_VIEW_TASK &&
          guard->view_task_id != NULL &&
          g_strcmp0(guard->view_task_id, task_id) == 0 &&
          guard->usage_task_view != NULL) {
        FocusGuardUsage *usage =
            focus_guard_usage_get_or_create(guard->usage_task_view, app_key, app_name);
        if (usage != NULL) {
          usage->usec_total += elapsed_us;
          guard->usage_dirty = TRUE;
        }
      }
    }
  }

  const char *task_title =
      active_task != NULL ? pomodoro_task_get_title(active_task) : NULL;

  gboolean chrome_relevance_allowed =
      tracking && guard->config.warnings_enabled && guard->ollama_available &&
      guard->config.chrome_ollama_enabled &&
      guard->config.ollama_model != NULL &&
      app_key != NULL && focus_guard_is_chrome_app(app_key);

  if (!chrome_relevance_allowed) {
    focus_guard_clear_relevance_warning(guard);
    if (guard->relevance_inflight) {
      focus_guard_cancel_relevance_check(guard);
    }
  } else if (!guard->relevance_inflight &&
             now_us - guard->last_relevance_check_us >=
                 (gint64)CHROME_RELEVANCE_INTERVAL_SECONDS * G_USEC_PER_SEC) {
    guard->last_relevance_check_us = now_us;
    focus_guard_start_relevance_check(guard, window_title, task_title);
  }

  if (!tracking || !guard->config.warnings_enabled || app_key == NULL) {
    focus_guard_set_warning(guard, FALSE, NULL);
  } else {
    guard->last_warning_check_us = now_us;
    focus_guard_refresh_warning(guard, app_name, app_key);
  }

  focus_guard_update_stats_ui(guard);

  g_free(window_title);
  g_free(app_key);
  g_free(app_name);

  return G_SOURCE_CONTINUE;
}
