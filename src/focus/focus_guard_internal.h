#pragma once

#include <gio/gio.h>
#include <gtk/gtk.h>

#include "focus/focus_guard.h"
#include "storage/usage_stats_storage.h"

typedef enum {
  FOCUS_GUARD_VIEW_GLOBAL = 0,
  FOCUS_GUARD_VIEW_TASK = 1
} FocusGuardView;

typedef enum {
  FOCUS_GUARD_RELEVANCE_UNKNOWN = 0,
  FOCUS_GUARD_RELEVANCE_RELEVANT = 1,
  FOCUS_GUARD_RELEVANCE_UNSURE = 2,
  FOCUS_GUARD_RELEVANCE_IRRELEVANT = 3
} FocusGuardRelevance;

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

GHashTable *focus_guard_usage_table_new(void);
GHashTable *focus_guard_bucket_task_table_new(void);
FocusGuardUsage *focus_guard_usage_get_or_create(GHashTable *table,
                                                 const char *key,
                                                 const char *display);
gboolean focus_guard_refresh_day(FocusGuard *guard);
void focus_guard_prune_history(FocusGuard *guard);
void focus_guard_load_usage_map_from_db(FocusGuard *guard,
                                        GHashTable *table,
                                        const char *scope,
                                        const char *task_id);
void focus_guard_merge_bucket_task(FocusGuard *guard,
                                   const char *task_id,
                                   GHashTable *table);
void focus_guard_flush_bucket(FocusGuard *guard);
void focus_guard_update_stats_ui(FocusGuard *guard);
gboolean focus_guard_on_tick(gpointer data);

void focus_guard_build_blacklist(FocusGuard *guard);
void focus_guard_set_warning(FocusGuard *guard,
                             gboolean active,
                             const char *text);
void focus_guard_refresh_warning(FocusGuard *guard,
                                 const char *app_name,
                                 const char *app_key);
void focus_guard_refresh_warning_from_active(FocusGuard *guard);
gboolean focus_guard_should_track(const FocusGuard *guard);
gboolean focus_guard_is_blacklisted(FocusGuard *guard, const char *app_key);
gboolean focus_guard_is_chrome_app(const char *app_key);

void focus_guard_clear_relevance_warning(FocusGuard *guard);
void focus_guard_start_relevance_check(FocusGuard *guard,
                                       const char *window_title,
                                       const char *task_title);
void focus_guard_cancel_relevance_check(FocusGuard *guard);
