#include "focus/focus_guard_internal.h"

#include "core/task_store.h"

#define USAGE_STATS_RETENTION_DAYS 35

static void
focus_guard_usage_free(gpointer data)
{
  FocusGuardUsage *usage = data;
  if (usage == NULL) {
    return;
  }

  g_free(usage->display_name);
  g_free(usage);
}

GHashTable *
focus_guard_usage_table_new(void)
{
  return g_hash_table_new_full(g_str_hash,
                               g_str_equal,
                               g_free,
                               focus_guard_usage_free);
}

static void
focus_guard_bucket_task_free(gpointer data)
{
  FocusGuardBucketTaskEntry *entry = data;
  if (entry == NULL) {
    return;
  }

  g_free(entry->task_id);
  g_free(entry->app_key);
  g_free(entry->app_name);
  g_free(entry);
}

GHashTable *
focus_guard_bucket_task_table_new(void)
{
  return g_hash_table_new_full(g_str_hash,
                               g_str_equal,
                               g_free,
                               focus_guard_bucket_task_free);
}

FocusGuardUsage *
focus_guard_usage_get_or_create(GHashTable *table,
                                const char *key,
                                const char *display)
{
  if (table == NULL || key == NULL) {
    return NULL;
  }

  FocusGuardUsage *usage = g_hash_table_lookup(table, key);
  if (usage != NULL) {
    return usage;
  }

  usage = g_new0(FocusGuardUsage, 1);
  usage->display_name = g_strdup(display ? display : key);
  usage->usec_total = 0;
  g_hash_table_insert(table, g_strdup(key), usage);
  return usage;
}

static void
focus_guard_get_day_bounds(gint64 *start_utc,
                           gint64 *end_utc,
                           char **label)
{
  if (start_utc == NULL || end_utc == NULL) {
    return;
  }

  GDateTime *now_local = g_date_time_new_now_local();
  if (now_local == NULL) {
    *start_utc = 0;
    *end_utc = 0;
    if (label != NULL) {
      *label = g_strdup("Today");
    }
    return;
  }

  gint year = g_date_time_get_year(now_local);
  gint month = g_date_time_get_month(now_local);
  gint day = g_date_time_get_day_of_month(now_local);

  GDateTime *start_local = g_date_time_new_local(year, month, day, 0, 0, 0);
  GDateTime *end_local = g_date_time_add_days(start_local, 1);

  *start_utc = g_date_time_to_unix(start_local);
  *end_utc = g_date_time_to_unix(end_local);

  if (label != NULL) {
    *label = g_date_time_format(start_local, "%a, %b %d, %Y");
  }

  g_date_time_unref(end_local);
  g_date_time_unref(start_local);
  g_date_time_unref(now_local);
}

gboolean
focus_guard_refresh_day(FocusGuard *guard)
{
  if (guard == NULL) {
    return FALSE;
  }

  gint64 start_utc = 0;
  gint64 end_utc = 0;
  char *label = NULL;
  focus_guard_get_day_bounds(&start_utc, &end_utc, &label);

  gboolean changed = (start_utc != guard->day_start_utc);
  if (changed) {
    guard->day_start_utc = start_utc;
    g_clear_pointer(&guard->day_label, g_free);
    guard->day_label = label;
  } else {
    g_free(label);
  }

  (void)end_utc;
  return changed;
}

static void
focus_guard_clear_usage_table(GHashTable *table)
{
  if (table == NULL) {
    return;
  }

  g_hash_table_remove_all(table);
}

void
focus_guard_load_usage_map_from_db(FocusGuard *guard,
                                   GHashTable *table,
                                   const char *scope,
                                   const char *task_id)
{
  if (guard == NULL || table == NULL) {
    return;
  }

  focus_guard_clear_usage_table(table);
  if (guard->stats_store == NULL) {
    return;
  }

  gint64 start_utc = 0;
  gint64 end_utc = 0;
  focus_guard_get_day_bounds(&start_utc, &end_utc, NULL);

  GPtrArray *entries = usage_stats_store_query_day(guard->stats_store,
                                                   start_utc,
                                                   end_utc,
                                                   scope,
                                                   task_id);
  if (entries == NULL) {
    return;
  }

  for (guint i = 0; i < entries->len; i++) {
    UsageStatsEntry *entry = g_ptr_array_index(entries, i);
    if (entry == NULL || entry->duration_sec <= 0 || entry->app_key == NULL) {
      continue;
    }
    FocusGuardUsage *usage =
        focus_guard_usage_get_or_create(table, entry->app_key, entry->app_name);
    if (usage != NULL) {
      usage->usec_total = entry->duration_sec * G_USEC_PER_SEC;
    }
  }

  g_ptr_array_free(entries, TRUE);
}

void
focus_guard_merge_bucket_task(FocusGuard *guard,
                              const char *task_id,
                              GHashTable *table)
{
  if (guard == NULL || guard->bucket_task == NULL || table == NULL ||
      task_id == NULL) {
    return;
  }

  GHashTableIter iter;
  gpointer key = NULL;
  gpointer value = NULL;
  g_hash_table_iter_init(&iter, guard->bucket_task);
  while (g_hash_table_iter_next(&iter, &key, &value)) {
    FocusGuardBucketTaskEntry *entry = value;
    if (entry == NULL || entry->usec_total <= 0 || entry->task_id == NULL ||
        entry->app_key == NULL) {
      continue;
    }
    if (g_strcmp0(entry->task_id, task_id) != 0) {
      continue;
    }

    FocusGuardUsage *usage =
        focus_guard_usage_get_or_create(table, entry->app_key, entry->app_name);
    if (usage != NULL) {
      usage->usec_total += entry->usec_total;
    }
  }
}

void
focus_guard_select_global(FocusGuard *guard)
{
  if (guard == NULL) {
    return;
  }

  guard->view = FOCUS_GUARD_VIEW_GLOBAL;
  g_clear_pointer(&guard->view_task_id, g_free);
  g_clear_pointer(&guard->view_task_title, g_free);
  if (guard->usage_task_view != NULL) {
    g_hash_table_destroy(guard->usage_task_view);
    guard->usage_task_view = NULL;
  }
  guard->usage_dirty = TRUE;
  focus_guard_update_stats_ui(guard);
}

void
focus_guard_select_task(FocusGuard *guard, PomodoroTask *task)
{
  if (guard == NULL || task == NULL) {
    focus_guard_select_global(guard);
    return;
  }

  guard->view = FOCUS_GUARD_VIEW_TASK;
  g_free(guard->view_task_id);
  g_free(guard->view_task_title);
  guard->view_task_id = g_strdup(pomodoro_task_get_id(task));
  guard->view_task_title = g_strdup(pomodoro_task_get_title(task));

  if (guard->usage_task_view == NULL) {
    guard->usage_task_view = focus_guard_usage_table_new();
  }

  focus_guard_load_usage_map_from_db(guard,
                                     guard->usage_task_view,
                                     "task",
                                     guard->view_task_id);
  focus_guard_merge_bucket_task(guard, guard->view_task_id, guard->usage_task_view);

  guard->usage_dirty = TRUE;
  focus_guard_update_stats_ui(guard);
}

void
focus_guard_flush_bucket(FocusGuard *guard)
{
  if (guard == NULL) {
    return;
  }

  if (guard->bucket_start_utc <= 0) {
    focus_guard_clear_usage_table(guard->bucket_global);
    if (guard->bucket_task != NULL) {
      g_hash_table_remove_all(guard->bucket_task);
    }
    return;
  }

  if (guard->stats_store == NULL) {
    focus_guard_clear_usage_table(guard->bucket_global);
    if (guard->bucket_task != NULL) {
      g_hash_table_remove_all(guard->bucket_task);
    }
    guard->bucket_start_utc = 0;
    return;
  }

  GHashTableIter iter;
  gpointer key = NULL;
  gpointer value = NULL;
  if (guard->bucket_global != NULL) {
    g_hash_table_iter_init(&iter, guard->bucket_global);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
      FocusGuardUsage *usage = value;
      const char *app_key = key;
      if (usage == NULL || usage->usec_total <= 0 || app_key == NULL) {
        continue;
      }
      gint64 seconds = usage->usec_total / G_USEC_PER_SEC;
      if (seconds <= 0) {
        continue;
      }
      usage_stats_store_add(guard->stats_store,
                            guard->bucket_start_utc,
                            "global",
                            NULL,
                            app_key,
                            usage->display_name ? usage->display_name : app_key,
                            seconds);
    }
  }

  if (guard->bucket_task != NULL) {
    g_hash_table_iter_init(&iter, guard->bucket_task);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
      FocusGuardBucketTaskEntry *entry = value;
      if (entry == NULL || entry->usec_total <= 0 || entry->task_id == NULL ||
          entry->app_key == NULL) {
        continue;
      }
      gint64 seconds = entry->usec_total / G_USEC_PER_SEC;
      if (seconds <= 0) {
        continue;
      }
      usage_stats_store_add(guard->stats_store,
                            guard->bucket_start_utc,
                            "task",
                            entry->task_id,
                            entry->app_key,
                            entry->app_name ? entry->app_name : entry->app_key,
                            seconds);
    }
  }

  focus_guard_clear_usage_table(guard->bucket_global);
  if (guard->bucket_task != NULL) {
    g_hash_table_remove_all(guard->bucket_task);
  }
  guard->bucket_start_utc = 0;
}

void
focus_guard_prune_history(FocusGuard *guard)
{
  if (guard == NULL || guard->stats_store == NULL || guard->day_start_utc <= 0) {
    return;
  }

  GDateTime *day_start_local = g_date_time_new_from_unix_local(guard->day_start_utc);
  if (day_start_local == NULL) {
    return;
  }

  GDateTime *cutoff_local =
      g_date_time_add_days(day_start_local, -(gint)USAGE_STATS_RETENTION_DAYS);
  gint64 cutoff_utc = g_date_time_to_unix(cutoff_local);

  usage_stats_store_prune(guard->stats_store, cutoff_utc);

  g_date_time_unref(cutoff_local);
  g_date_time_unref(day_start_local);
}

void
focus_guard_clear_stats(FocusGuard *guard)
{
  if (guard == NULL) {
    return;
  }

  if (guard->stats_store != NULL) {
    usage_stats_store_clear(guard->stats_store);
  }

  focus_guard_clear_usage_table(guard->usage_global);
  focus_guard_clear_usage_table(guard->usage_task_view);
  focus_guard_clear_usage_table(guard->bucket_global);
  if (guard->bucket_task != NULL) {
    g_hash_table_remove_all(guard->bucket_task);
  }
  guard->bucket_start_utc = 0;
  guard->usage_dirty = TRUE;
  focus_guard_update_stats_ui(guard);
}
