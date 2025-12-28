#include "storage/usage_stats_storage.h"

#include <errno.h>
#include <sqlite3.h>

struct _UsageStatsStore {
  sqlite3 *db;
  sqlite3_stmt *stmt_upsert;
};

static char *
usage_stats_storage_get_path(void)
{
  return g_build_filename(g_get_user_data_dir(),
                          "floating-pomodoro",
                          "usage_stats.sqlite3",
                          NULL);
}

static gboolean
usage_stats_storage_ensure_dir(const char *path, GError **error)
{
  char *dir = g_path_get_dirname(path);
  if (g_mkdir_with_parents(dir, 0755) != 0) {
    g_set_error(error,
                G_FILE_ERROR,
                g_file_error_from_errno(errno),
                "Failed to create data directory '%s'",
                dir);
    g_free(dir);
    return FALSE;
  }
  g_free(dir);
  return TRUE;
}

static gboolean
usage_stats_store_exec(UsageStatsStore *store, const char *sql)
{
  if (store == NULL || store->db == NULL || sql == NULL) {
    return FALSE;
  }

  char *errmsg = NULL;
  if (sqlite3_exec(store->db, sql, NULL, NULL, &errmsg) != SQLITE_OK) {
    g_warning("Usage stats SQL error: %s", errmsg ? errmsg : "unknown error");
    sqlite3_free(errmsg);
    return FALSE;
  }

  return TRUE;
}

static gboolean
usage_stats_store_init(UsageStatsStore *store)
{
  if (store == NULL || store->db == NULL) {
    return FALSE;
  }

  if (!usage_stats_store_exec(store,
                              "CREATE TABLE IF NOT EXISTS app_usage ("
                              "bucket_start INTEGER NOT NULL,"
                              "scope TEXT NOT NULL,"
                              "task_id TEXT,"
                              "app_key TEXT NOT NULL,"
                              "app_name TEXT NOT NULL,"
                              "duration_sec INTEGER NOT NULL,"
                              "PRIMARY KEY (bucket_start, scope, task_id, app_key)"
                              ")")) {
    return FALSE;
  }

  if (!usage_stats_store_exec(store,
                              "CREATE INDEX IF NOT EXISTS idx_app_usage_scope_day "
                              "ON app_usage (scope, task_id, bucket_start)")) {
    return FALSE;
  }

  const char *sql =
      "INSERT INTO app_usage (bucket_start, scope, task_id, app_key, app_name, duration_sec) "
      "VALUES (?1, ?2, ?3, ?4, ?5, ?6) "
      "ON CONFLICT(bucket_start, scope, task_id, app_key) DO UPDATE SET "
      "duration_sec = duration_sec + excluded.duration_sec, "
      "app_name = excluded.app_name";

  if (sqlite3_prepare_v2(store->db, sql, -1, &store->stmt_upsert, NULL) != SQLITE_OK) {
    g_warning("Failed to prepare usage stats upsert statement: %s",
              sqlite3_errmsg(store->db));
    return FALSE;
  }

  return TRUE;
}

UsageStatsStore *
usage_stats_store_new(void)
{
  UsageStatsStore *store = g_new0(UsageStatsStore, 1);

  char *path = usage_stats_storage_get_path();
  GError *error = NULL;
  if (!usage_stats_storage_ensure_dir(path, &error)) {
    g_warning("Failed to prepare usage stats directory: %s",
              error ? error->message : "unknown error");
    g_clear_error(&error);
    g_free(path);
    g_free(store);
    return NULL;
  }

  if (sqlite3_open(path, &store->db) != SQLITE_OK) {
    g_warning("Failed to open usage stats database: %s",
              sqlite3_errmsg(store->db));
    sqlite3_close(store->db);
    g_free(path);
    g_free(store);
    return NULL;
  }

  sqlite3_busy_timeout(store->db, 1000);

  if (!usage_stats_store_init(store)) {
    usage_stats_store_free(store);
    g_free(path);
    return NULL;
  }

  g_free(path);
  return store;
}

void
usage_stats_store_free(UsageStatsStore *store)
{
  if (store == NULL) {
    return;
  }

  if (store->stmt_upsert != NULL) {
    sqlite3_finalize(store->stmt_upsert);
    store->stmt_upsert = NULL;
  }

  if (store->db != NULL) {
    sqlite3_close(store->db);
    store->db = NULL;
  }

  g_free(store);
}

gboolean
usage_stats_store_add(UsageStatsStore *store,
                      gint64 bucket_start_utc,
                      const char *scope,
                      const char *task_id,
                      const char *app_key,
                      const char *app_name,
                      gint64 duration_sec)
{
  if (store == NULL || store->db == NULL || store->stmt_upsert == NULL ||
      scope == NULL || app_key == NULL || app_name == NULL) {
    return FALSE;
  }

  if (duration_sec <= 0) {
    return TRUE;
  }

  sqlite3_stmt *stmt = store->stmt_upsert;
  sqlite3_reset(stmt);
  sqlite3_clear_bindings(stmt);

  sqlite3_bind_int64(stmt, 1, bucket_start_utc);
  sqlite3_bind_text(stmt, 2, scope, -1, SQLITE_TRANSIENT);
  if (task_id != NULL) {
    sqlite3_bind_text(stmt, 3, task_id, -1, SQLITE_TRANSIENT);
  } else {
    sqlite3_bind_null(stmt, 3);
  }
  sqlite3_bind_text(stmt, 4, app_key, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 5, app_name, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 6, duration_sec);

  if (sqlite3_step(stmt) != SQLITE_DONE) {
    g_warning("Failed to write usage stats: %s", sqlite3_errmsg(store->db));
    return FALSE;
  }

  return TRUE;
}

GPtrArray *
usage_stats_store_query_day(UsageStatsStore *store,
                            gint64 day_start_utc,
                            gint64 day_end_utc,
                            const char *scope,
                            const char *task_id)
{
  if (store == NULL || store->db == NULL || scope == NULL) {
    return NULL;
  }

  const char *sql_global =
      "SELECT app_key, MAX(app_name) AS app_name, SUM(duration_sec) AS total "
      "FROM app_usage "
      "WHERE scope = ?1 AND task_id IS NULL AND bucket_start >= ?2 AND bucket_start < ?3 "
      "GROUP BY app_key "
      "ORDER BY total DESC";
  const char *sql_task =
      "SELECT app_key, MAX(app_name) AS app_name, SUM(duration_sec) AS total "
      "FROM app_usage "
      "WHERE scope = ?1 AND task_id = ?2 AND bucket_start >= ?3 AND bucket_start < ?4 "
      "GROUP BY app_key "
      "ORDER BY total DESC";

  sqlite3_stmt *stmt = NULL;
  if (task_id == NULL) {
    if (sqlite3_prepare_v2(store->db, sql_global, -1, &stmt, NULL) != SQLITE_OK) {
      g_warning("Failed to prepare usage stats query: %s", sqlite3_errmsg(store->db));
      return NULL;
    }
    sqlite3_bind_text(stmt, 1, scope, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, day_start_utc);
    sqlite3_bind_int64(stmt, 3, day_end_utc);
  } else {
    if (sqlite3_prepare_v2(store->db, sql_task, -1, &stmt, NULL) != SQLITE_OK) {
      g_warning("Failed to prepare usage stats query: %s", sqlite3_errmsg(store->db));
      return NULL;
    }
    sqlite3_bind_text(stmt, 1, scope, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, task_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, day_start_utc);
    sqlite3_bind_int64(stmt, 4, day_end_utc);
  }

  GPtrArray *entries = g_ptr_array_new_with_free_func(usage_stats_entry_free);

  for (;;) {
    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
      const char *app_key = (const char *)sqlite3_column_text(stmt, 0);
      const char *app_name = (const char *)sqlite3_column_text(stmt, 1);
      gint64 total = sqlite3_column_int64(stmt, 2);

      if (app_key == NULL || app_name == NULL || total <= 0) {
        continue;
      }

      UsageStatsEntry *entry = g_new0(UsageStatsEntry, 1);
      entry->app_key = g_strdup(app_key);
      entry->app_name = g_strdup(app_name);
      entry->duration_sec = total;
      g_ptr_array_add(entries, entry);
      continue;
    }

    if (rc == SQLITE_DONE) {
      break;
    }

    g_warning("Failed to read usage stats: %s", sqlite3_errmsg(store->db));
    break;
  }

  sqlite3_finalize(stmt);
  return entries;
}

gboolean
usage_stats_store_clear(UsageStatsStore *store)
{
  if (store == NULL || store->db == NULL) {
    return FALSE;
  }

  return usage_stats_store_exec(store, "DELETE FROM app_usage");
}

gboolean
usage_stats_store_prune(UsageStatsStore *store, gint64 cutoff_utc)
{
  if (store == NULL || store->db == NULL) {
    return FALSE;
  }

  sqlite3_stmt *stmt = NULL;
  const char *sql = "DELETE FROM app_usage WHERE bucket_start < ?1";
  if (sqlite3_prepare_v2(store->db, sql, -1, &stmt, NULL) != SQLITE_OK) {
    g_warning("Failed to prepare usage stats prune: %s", sqlite3_errmsg(store->db));
    return FALSE;
  }

  sqlite3_bind_int64(stmt, 1, cutoff_utc);
  gboolean ok = TRUE;
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    g_warning("Failed to prune usage stats: %s", sqlite3_errmsg(store->db));
    ok = FALSE;
  }

  sqlite3_finalize(stmt);
  return ok;
}

void
usage_stats_entry_free(gpointer data)
{
  UsageStatsEntry *entry = data;
  if (entry == NULL) {
    return;
  }

  g_free(entry->app_key);
  g_free(entry->app_name);
  g_free(entry);
}
