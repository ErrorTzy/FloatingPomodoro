#pragma once

#include <glib.h>

typedef struct _UsageStatsStore UsageStatsStore;

typedef struct {
  char *app_key;
  char *app_name;
  gint64 duration_sec;
} UsageStatsEntry;

UsageStatsStore *usage_stats_store_new(void);
void usage_stats_store_free(UsageStatsStore *store);

gboolean usage_stats_store_add(UsageStatsStore *store,
                               gint64 bucket_start_utc,
                               const char *scope,
                               const char *task_id,
                               const char *app_key,
                               const char *app_name,
                               gint64 duration_sec);

GPtrArray *usage_stats_store_query_day(UsageStatsStore *store,
                                       gint64 day_start_utc,
                                       gint64 day_end_utc,
                                       const char *scope,
                                       const char *task_id);

gboolean usage_stats_store_clear(UsageStatsStore *store);
gboolean usage_stats_store_prune(UsageStatsStore *store, gint64 cutoff_utc);

void usage_stats_entry_free(gpointer data);
