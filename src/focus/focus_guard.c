#include "focus/focus_guard.h"

#include <gio/gio.h>

#include "core/pomodoro_timer.h"
#include "core/task_store.h"
#include "focus/chrome_cdp_client.h"
#include "focus/focus_guard_x11.h"
#include "focus/ollama_client.h"
#include "overlay/overlay_window.h"
#include "storage/usage_stats_storage.h"

#define USAGE_BUCKET_SECONDS 300
#define USAGE_STATS_RETENTION_DAYS 35
#define CHROME_RELEVANCE_INTERVAL_SECONDS 15

typedef enum {
  FOCUS_GUARD_VIEW_GLOBAL = 0,
  FOCUS_GUARD_VIEW_TASK = 1
} FocusGuardView;

typedef struct {
  char *display_name;
  gint64 usec_total;
} FocusGuardUsage;

typedef struct {
  char *task_id;
  char *app_key;
  char *app_name;
  gint64 usec_total;
} FocusGuardBucketTaskEntry;

typedef enum {
  FOCUS_GUARD_RELEVANCE_UNKNOWN = 0,
  FOCUS_GUARD_RELEVANCE_RELEVANT = 1,
  FOCUS_GUARD_RELEVANCE_UNSURE = 2,
  FOCUS_GUARD_RELEVANCE_IRRELEVANT = 3
} FocusGuardRelevance;

typedef struct {
  GWeakRef window_ref;
  guint64 check_id;
  char *task_title;
  char *window_title;
  char *model;
  guint port;
} FocusGuardRelevanceContext;

typedef struct {
  FocusGuardRelevance verdict;
  ChromeCdpPage *page;
  char *raw_response;
} FocusGuardRelevanceResult;

static void focus_guard_bucket_task_free(gpointer data);

struct _FocusGuard {
  AppState *state;
  FocusGuardConfig config;
  char **blacklist_norm;
  UsageStatsStore *stats_store;
  GHashTable *usage_global;
  GHashTable *usage_task_view;
  GHashTable *bucket_global;
  GHashTable *bucket_task;
  gint64 bucket_start_utc;
  guint tick_source_id;
  gint64 last_tick_us;
  gint64 last_tick_real_us;
  gint64 last_warning_check_us;
  gint64 day_start_utc;
  char *day_label;
  FocusGuardView view;
  char *view_task_id;
  char *view_task_title;
  gboolean warning_active;
  char *warning_app;
  gboolean usage_dirty;
  gboolean ollama_available;
  gboolean relevance_warning_active;
  char *relevance_warning_text;
  FocusGuardRelevance relevance_state;
  gint64 last_relevance_check_us;
  guint64 relevance_check_id;
  gboolean relevance_inflight;
  GCancellable *relevance_cancellable;
};

static gint
focus_guard_usage_compare_desc(gconstpointer a, gconstpointer b)
{
  const FocusGuardUsage *left = *(const FocusGuardUsage *const *)a;
  const FocusGuardUsage *right = *(const FocusGuardUsage *const *)b;
  if (left == right) {
    return 0;
  }
  if (left == NULL) {
    return 1;
  }
  if (right == NULL) {
    return -1;
  }
  if (left->usec_total < right->usec_total) {
    return 1;
  }
  if (left->usec_total > right->usec_total) {
    return -1;
  }

  const char *left_name = left->display_name != NULL ? left->display_name : "";
  const char *right_name = right->display_name != NULL ? right->display_name : "";
  gint name_cmp = g_ascii_strcasecmp(left_name, right_name);
  if (name_cmp != 0) {
    return name_cmp;
  }
  return g_strcmp0(left_name, right_name);
}


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

static GHashTable *
focus_guard_usage_table_new(void)
{
  return g_hash_table_new_full(g_str_hash,
                               g_str_equal,
                               g_free,
                               focus_guard_usage_free);
}

static GHashTable *
focus_guard_bucket_task_table_new(void)
{
  return g_hash_table_new_full(g_str_hash,
                               g_str_equal,
                               g_free,
                               focus_guard_bucket_task_free);
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

static void
focus_guard_set_warning(FocusGuard *guard, gboolean active, const char *text)
{
  if (guard == NULL || guard->state == NULL) {
    return;
  }

  if (!active) {
    if (!guard->warning_active) {
      return;
    }
    guard->warning_active = FALSE;
    g_clear_pointer(&guard->warning_app, g_free);
    overlay_window_set_warning(guard->state, FALSE, NULL);
    return;
  }

  if (!guard->warning_active ||
      g_strcmp0(guard->warning_app, text) != 0) {
    g_free(guard->warning_app);
    guard->warning_app = g_strdup(text != NULL ? text : "");
  }

  guard->warning_active = TRUE;

  if (!overlay_window_is_visible(guard->state)) {
    overlay_window_set_visible(guard->state, TRUE);
  }

  overlay_window_set_warning(guard->state, TRUE, guard->warning_app);
}

static gboolean
focus_guard_should_track(const FocusGuard *guard)
{
  if (guard == NULL || guard->state == NULL || guard->state->timer == NULL ||
      guard->state->store == NULL) {
    return FALSE;
  }

  PomodoroTimerState run_state = pomodoro_timer_get_state(guard->state->timer);
  if (run_state != POMODORO_TIMER_RUNNING) {
    return FALSE;
  }

  PomodoroPhase phase = pomodoro_timer_get_phase(guard->state->timer);
  if (phase != POMODORO_PHASE_FOCUS) {
    return FALSE;
  }

  return task_store_get_active(guard->state->store) != NULL;
}


static void
focus_guard_build_blacklist(FocusGuard *guard)
{
  if (guard == NULL) {
    return;
  }

  g_strfreev(guard->blacklist_norm);
  guard->blacklist_norm = NULL;

  if (guard->config.blacklist == NULL) {
    guard->blacklist_norm = g_new0(char *, 1);
    return;
  }

  gsize count = g_strv_length(guard->config.blacklist);
  guard->blacklist_norm = g_new0(char *, count + 1);

  for (gsize i = 0; i < count; i++) {
    const char *value = guard->config.blacklist[i];
    if (value == NULL) {
      continue;
    }
    guard->blacklist_norm[i] = g_ascii_strdown(value, -1);
  }
}

static gboolean
focus_guard_is_blacklisted(FocusGuard *guard, const char *app_key)
{
  if (guard == NULL || app_key == NULL || guard->blacklist_norm == NULL) {
    return FALSE;
  }

  for (gsize i = 0; guard->blacklist_norm[i] != NULL; i++) {
    const char *value = guard->blacklist_norm[i];
    if (value == NULL || *value == '\0') {
      continue;
    }
    if (g_strstr_len(app_key, -1, value) != NULL) {
      return TRUE;
    }
  }

  return FALSE;
}

static FocusGuardUsage *
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

static char *
focus_guard_format_duration(gint64 seconds)
{
  if (seconds < 0) {
    seconds = 0;
  }

  gint64 hours = seconds / 3600;
  gint64 minutes = (seconds % 3600) / 60;
  gint64 secs = seconds % 60;

  if (hours > 0) {
    return g_strdup_printf("%" G_GINT64_FORMAT "h %02" G_GINT64_FORMAT "m",
                           hours,
                           minutes);
  }

  return g_strdup_printf("%" G_GINT64_FORMAT "m %02" G_GINT64_FORMAT "s",
                         minutes,
                         secs);
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

static gboolean
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
focus_guard_clear_list(GtkWidget *list)
{
  if (list == NULL) {
    return;
  }

  GtkWidget *child = gtk_widget_get_first_child(list);
  while (child != NULL) {
    GtkWidget *next = gtk_widget_get_next_sibling(child);
    gtk_list_box_remove(GTK_LIST_BOX(list), child);
    child = next;
  }
}

static void
focus_guard_update_stats_header(FocusGuard *guard)
{
  if (guard == NULL || guard->state == NULL) {
    return;
  }

  if (guard->state->focus_stats_context_label != NULL) {
    const char *text = NULL;
    if (guard->view == FOCUS_GUARD_VIEW_TASK) {
      if (guard->view_task_title != NULL) {
        char *label = g_strdup_printf("Task: %s", guard->view_task_title);
        gtk_label_set_text(GTK_LABEL(guard->state->focus_stats_context_label), label);
        g_free(label);
      } else {
        gtk_label_set_text(GTK_LABEL(guard->state->focus_stats_context_label),
                           "Task stats");
      }
    } else {
      if (!guard->config.global_stats_enabled) {
        text = "Global stats (disabled)";
      } else {
        text = "Global stats";
      }
      gtk_label_set_text(GTK_LABEL(guard->state->focus_stats_context_label),
                         text);
    }
  }

  if (guard->state->focus_stats_day_label != NULL) {
    const char *day_text = guard->day_label != NULL ? guard->day_label : "Today";
    gtk_label_set_text(GTK_LABEL(guard->state->focus_stats_day_label), day_text);
  }
}

static void
focus_guard_update_stats_ui(FocusGuard *guard)
{
  if (guard == NULL || guard->state == NULL ||
      guard->state->focus_stats_list == NULL) {
    return;
  }

  if (!guard->usage_dirty) {
    return;
  }

  focus_guard_refresh_day(guard);
  focus_guard_update_stats_header(guard);

  const char *empty_text = NULL;
  GHashTable *source = NULL;
  if (guard->view == FOCUS_GUARD_VIEW_TASK) {
    source = guard->usage_task_view;
    empty_text = guard->view_task_id != NULL
                     ? "No app activity yet for this task."
                     : "Select a task to view stats.";
  } else if (!guard->config.global_stats_enabled) {
    source = NULL;
    empty_text = "Global stats disabled.";
  } else {
    source = guard->usage_global;
    empty_text = "No app activity yet.";
  }

  GPtrArray *entries = g_ptr_array_new();
  if (source != NULL) {
    GHashTableIter iter;
    gpointer key = NULL;
    gpointer value = NULL;
    g_hash_table_iter_init(&iter, source);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
      FocusGuardUsage *usage = value;
      if (usage == NULL || usage->usec_total <= 0) {
        continue;
      }
      g_ptr_array_add(entries, usage);
    }
  }

  g_ptr_array_sort(entries, focus_guard_usage_compare_desc);

  focus_guard_clear_list(guard->state->focus_stats_list);

  const guint max_rows = 5;
  guint shown = 0;
  for (guint i = 0; i < entries->len && shown < max_rows; i++) {
    FocusGuardUsage *usage = g_ptr_array_index(entries, i);
    if (usage == NULL) {
      continue;
    }

    GtkWidget *row = gtk_list_box_row_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_hexpand(box, TRUE);

    GtkWidget *app_label = gtk_label_new(usage->display_name);
    gtk_widget_add_css_class(app_label, "focus-guard-app");
    gtk_widget_set_halign(app_label, GTK_ALIGN_START);
    gtk_label_set_ellipsize(GTK_LABEL(app_label), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(app_label, TRUE);

    char *duration = focus_guard_format_duration(usage->usec_total / G_USEC_PER_SEC);
    GtkWidget *time_label = gtk_label_new(duration);
    gtk_widget_add_css_class(time_label, "focus-guard-time");
    gtk_widget_set_halign(time_label, GTK_ALIGN_END);
    g_free(duration);

    gtk_box_append(GTK_BOX(box), app_label);
    gtk_box_append(GTK_BOX(box), time_label);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
    gtk_list_box_append(GTK_LIST_BOX(guard->state->focus_stats_list), row);
    shown++;
  }

  if (guard->state->focus_stats_empty_label != NULL) {
    gtk_label_set_text(GTK_LABEL(guard->state->focus_stats_empty_label),
                       empty_text ? empty_text : "No app activity yet.");
    gtk_widget_set_visible(guard->state->focus_stats_empty_label, shown == 0);
  }

  guard->usage_dirty = FALSE;
  g_ptr_array_free(entries, TRUE);
}

static void
focus_guard_clear_usage_table(GHashTable *table)
{
  if (table == NULL) {
    return;
  }

  g_hash_table_remove_all(table);
}

static void
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

static void
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

static void
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

static gboolean
focus_guard_is_chrome_app(const char *app_key)
{
  if (app_key == NULL || *app_key == '\0') {
    return FALSE;
  }

  return g_strstr_len(app_key, -1, "chrome") != NULL ||
         g_strstr_len(app_key, -1, "chromium") != NULL;
}

static void
focus_guard_clear_relevance_warning(FocusGuard *guard)
{
  if (guard == NULL) {
    return;
  }

  guard->relevance_warning_active = FALSE;
  guard->relevance_state = FOCUS_GUARD_RELEVANCE_UNKNOWN;
  g_clear_pointer(&guard->relevance_warning_text, g_free);
}

static char *
focus_guard_truncate_label(char *text)
{
  if (text == NULL) {
    return NULL;
  }

  const guint max_chars = 80;
  if (g_utf8_strlen(text, -1) <= max_chars) {
    return text;
  }

  char *shortened = g_utf8_substring(text, 0, max_chars - 3);
  char *result = g_strconcat(shortened, "...", NULL);
  g_free(shortened);
  g_free(text);
  return result;
}

static char *
focus_guard_format_relevance_warning(const ChromeCdpPage *page)
{
  if (page != NULL && page->title != NULL && *page->title != '\0') {
    char *label = g_strdup_printf("Chrome: %s", page->title);
    return focus_guard_truncate_label(label);
  }

  if (page != NULL && page->url != NULL && *page->url != '\0') {
    char *label = g_strdup_printf("Chrome: %s", page->url);
    return focus_guard_truncate_label(label);
  }

  return g_strdup("Chrome off-task");
}

static void
focus_guard_refresh_warning(FocusGuard *guard,
                            const char *app_name,
                            const char *app_key)
{
  if (guard == NULL) {
    return;
  }

  if (!focus_guard_should_track(guard) || !guard->config.warnings_enabled) {
    focus_guard_set_warning(guard, FALSE, NULL);
    return;
  }

  if (app_key != NULL && focus_guard_is_blacklisted(guard, app_key)) {
    focus_guard_set_warning(guard, TRUE, app_name != NULL ? app_name : app_key);
    return;
  }

  if (guard->relevance_warning_active &&
      app_key != NULL &&
      focus_guard_is_chrome_app(app_key)) {
    focus_guard_set_warning(guard,
                            TRUE,
                            guard->relevance_warning_text != NULL
                                ? guard->relevance_warning_text
                                : "Chrome");
    return;
  }

  focus_guard_set_warning(guard, FALSE, NULL);
}

static void
focus_guard_refresh_warning_from_active(FocusGuard *guard)
{
  if (guard == NULL) {
    return;
  }

  char *app_name = NULL;
  char *app_key = NULL;
  if (focus_guard_x11_get_active_app(&app_name, NULL) && app_name != NULL) {
    app_key = g_ascii_strdown(app_name, -1);
  }

  focus_guard_refresh_warning(guard, app_name, app_key);

  g_free(app_key);
  g_free(app_name);
}

static FocusGuardRelevance
focus_guard_parse_relevance_response(const char *response)
{
  if (response == NULL) {
    return FOCUS_GUARD_RELEVANCE_UNKNOWN;
  }

  char *lower = g_ascii_strdown(response, -1);
  FocusGuardRelevance verdict = FOCUS_GUARD_RELEVANCE_UNSURE;
  if (g_strstr_len(lower, -1, "clearly irrelevant") != NULL) {
    verdict = FOCUS_GUARD_RELEVANCE_IRRELEVANT;
  } else if (g_strstr_len(lower, -1, "directly relevant") != NULL) {
    verdict = FOCUS_GUARD_RELEVANCE_RELEVANT;
  } else if (g_strstr_len(lower, -1, "not sure") != NULL) {
    verdict = FOCUS_GUARD_RELEVANCE_UNSURE;
  }
  g_free(lower);
  return verdict;
}

static void
focus_guard_relevance_result_free(gpointer data)
{
  FocusGuardRelevanceResult *result = data;
  if (result == NULL) {
    return;
  }

  chrome_cdp_page_free(result->page);
  g_free(result->raw_response);
  g_free(result);
}

static void
focus_guard_relevance_context_free(gpointer data)
{
  FocusGuardRelevanceContext *context = data;
  if (context == NULL) {
    return;
  }

  g_weak_ref_clear(&context->window_ref);
  g_free(context->task_title);
  g_free(context->window_title);
  g_free(context->model);
  g_free(context);
}

static const char *
focus_guard_system_prompt(void)
{
  return "You are a focus assistant that checks if a web page is relevant to the user's task."
         " Reply with exactly one label: directly relevant, not sure, or clearly irrelevant."
         " Use the content inside XML-like tags to decide.\n"
         "\n"
         "Examples:\n"
         "<task-title>Draft Q4 budget report</task-title>\n"
         "<page-title>Q4 Budget — Google Sheets</page-title>\n"
         "<page-content>Revenue, expenses, forecasts, variance notes...</page-content>\n"
         "Answer: directly relevant\n"
         "---\n"
         "<task-title>Draft Q4 budget report</task-title>\n"
         "<page-title>YouTube — Lo-fi hip hop</page-title>\n"
         "<page-content>Playlists, comments, music channels...</page-content>\n"
         "Answer: clearly irrelevant\n"
         "---\n"
         "<task-title>Study GTK4 layout</task-title>\n"
         "<page-title>GTK4 Box and Grid — GNOME Developer</page-title>\n"
         "<page-content>GtkBox, GtkGrid, layout examples...</page-content>\n"
         "Answer: directly relevant\n"
         "---\n"
         "<task-title>Plan a workshop agenda</task-title>\n"
         "<page-title>Hacker News</page-title>\n"
         "<page-content>Top stories, comments, unrelated news...</page-content>\n"
         "Answer: not sure\n"
         "\n"
         "Return only the label.";
}

static char *
focus_guard_escape_xml(const char *text)
{
  if (text == NULL) {
    return g_strdup("");
  }

  return g_markup_escape_text(text, -1);
}

static char *
focus_guard_build_user_prompt(const char *task_title, const ChromeCdpPage *page)
{
  char *task = focus_guard_escape_xml(task_title);
  char *title = focus_guard_escape_xml(page != NULL ? page->title : NULL);
  char *url = focus_guard_escape_xml(page != NULL ? page->url : NULL);
  char *content = focus_guard_escape_xml(page != NULL ? page->text : NULL);

  GString *prompt = g_string_new(NULL);
  g_string_append(prompt, "<context>\n");
  g_string_append_printf(prompt, "  <task-title>%s</task-title>\n", task);
  g_string_append(prompt, "  <page>\n");
  g_string_append_printf(prompt, "    <page-title>%s</page-title>\n", title);
  g_string_append_printf(prompt, "    <page-url>%s</page-url>\n", url);
  g_string_append(prompt, "    <page-content>\n");
  g_string_append(prompt, content);
  g_string_append(prompt, "\n    </page-content>\n");
  g_string_append(prompt, "  </page>\n");
  g_string_append(prompt, "</context>\n\n");
  g_string_append(prompt,
                  "Answer with exactly one label: directly relevant, not sure, or clearly irrelevant.");

  g_free(task);
  g_free(title);
  g_free(url);
  g_free(content);

  return g_string_free(prompt, FALSE);
}

static void
focus_guard_relevance_task(GTask *task,
                           gpointer source_object,
                           gpointer task_data,
                           GCancellable *cancellable)
{
  (void)source_object;
  FocusGuardRelevanceContext *context = task_data;
  if (context == NULL) {
    g_task_return_new_error(task,
                            G_IO_ERROR,
                            G_IO_ERROR_INVALID_ARGUMENT,
                            "Relevance context missing");
    return;
  }

  GError *error = NULL;
  ChromeCdpPage *page = chrome_cdp_fetch_page_sync(context->port,
                                                   context->window_title,
                                                   cancellable,
                                                   &error);
  if (page == NULL) {
    g_task_return_error(task, error);
    return;
  }

  char *user_prompt = focus_guard_build_user_prompt(context->task_title, page);
  char *response =
      ollama_client_chat_sync(context->model,
                              focus_guard_system_prompt(),
                              user_prompt,
                              cancellable,
                              &error);
  g_free(user_prompt);

  if (response == NULL) {
    chrome_cdp_page_free(page);
    g_task_return_error(task, error);
    return;
  }

  FocusGuardRelevanceResult *result = g_new0(FocusGuardRelevanceResult, 1);
  result->raw_response = response;
  result->page = page;
  result->verdict = focus_guard_parse_relevance_response(response);
  g_task_return_pointer(task, result, focus_guard_relevance_result_free);
}

static void
focus_guard_on_relevance_task_complete(GObject *source_object,
                                       GAsyncResult *res,
                                       gpointer user_data)
{
  (void)source_object;
  (void)user_data;
  FocusGuardRelevanceContext *context =
      g_task_get_task_data(G_TASK(res));
  if (context == NULL) {
    return;
  }

  GtkWindow *window = g_weak_ref_get(&context->window_ref);
  if (window == NULL) {
    return;
  }

  AppState *state = g_object_get_data(G_OBJECT(window), "app-state");
  FocusGuard *guard = state != NULL ? state->focus_guard : NULL;
  if (guard == NULL || context->check_id != guard->relevance_check_id) {
    g_object_unref(window);
    return;
  }

  guard->relevance_inflight = FALSE;
  g_clear_object(&guard->relevance_cancellable);

  GError *error = NULL;
  FocusGuardRelevanceResult *result =
      g_task_propagate_pointer(G_TASK(res), &error);

  if (result == NULL || error != NULL) {
    if (error != NULL) {
      g_debug("Chrome relevance check failed: %s", error->message);
      g_clear_error(&error);
    }
    focus_guard_clear_relevance_warning(guard);
    focus_guard_refresh_warning_from_active(guard);
    g_object_unref(window);
    return;
  }

  guard->relevance_state = result->verdict;
  if (result->verdict == FOCUS_GUARD_RELEVANCE_IRRELEVANT) {
    guard->relevance_warning_active = TRUE;
    g_free(guard->relevance_warning_text);
    guard->relevance_warning_text = focus_guard_format_relevance_warning(result->page);
  } else {
    focus_guard_clear_relevance_warning(guard);
  }

  focus_guard_refresh_warning_from_active(guard);
  focus_guard_relevance_result_free(result);
  g_object_unref(window);
}

static void
focus_guard_start_relevance_check(FocusGuard *guard,
                                  const char *window_title,
                                  const char *task_title)
{
  if (guard == NULL || guard->state == NULL) {
    return;
  }

  if (guard->relevance_inflight) {
    return;
  }

  if (guard->config.ollama_model == NULL ||
      *guard->config.ollama_model == '\0') {
    return;
  }

  if (task_title == NULL || *task_title == '\0') {
    return;
  }

  guard->relevance_inflight = TRUE;
  guard->relevance_check_id++;
  guard->relevance_cancellable = g_cancellable_new();

  FocusGuardRelevanceContext *context = g_new0(FocusGuardRelevanceContext, 1);
  g_weak_ref_init(&context->window_ref, G_OBJECT(guard->state->window));
  context->check_id = guard->relevance_check_id;
  context->task_title = g_strdup(task_title);
  context->window_title = g_strdup(window_title);
  context->model = g_strdup(guard->config.ollama_model);
  context->port = guard->config.chrome_debug_port;

  GTask *task = g_task_new(NULL,
                           guard->relevance_cancellable,
                           focus_guard_on_relevance_task_complete,
                           NULL);
  g_task_set_task_data(task, context, focus_guard_relevance_context_free);
  g_task_run_in_thread(task, focus_guard_relevance_task);
  g_object_unref(task);
}

static void
focus_guard_cancel_relevance_check(FocusGuard *guard)
{
  if (guard == NULL) {
    return;
  }

  if (guard->relevance_cancellable != NULL) {
    g_cancellable_cancel(guard->relevance_cancellable);
    g_clear_object(&guard->relevance_cancellable);
  }

  guard->relevance_inflight = FALSE;
  guard->relevance_check_id++;
}

static gboolean
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
