#include "storage/task_storage.h"

#include <errno.h>
#include <string.h>

static const char *
task_status_to_string(TaskStatus status)
{
  switch (status) {
    case TASK_STATUS_ACTIVE:
      return "active";
    case TASK_STATUS_COMPLETED:
      return "completed";
    case TASK_STATUS_ARCHIVED:
      return "archived";
    default:
      return "active";
  }
}

static TaskStatus
task_status_from_string(const char *value)
{
  if (value == NULL) {
    return TASK_STATUS_ACTIVE;
  }

  if (g_ascii_strcasecmp(value, "completed") == 0) {
    return TASK_STATUS_COMPLETED;
  }

  if (g_ascii_strcasecmp(value, "archived") == 0) {
    return TASK_STATUS_ARCHIVED;
  }

  return TASK_STATUS_ACTIVE;
}

static const char *
archive_strategy_to_string(TaskArchiveStrategyType type)
{
  switch (type) {
    case TASK_ARCHIVE_IMMEDIATE:
      return "immediate";
    case TASK_ARCHIVE_KEEP_LATEST:
      return "keep_latest";
    case TASK_ARCHIVE_AFTER_DAYS:
    default:
      return "after_days";
  }
}

static TaskArchiveStrategyType
archive_strategy_from_string(const char *value)
{
  if (value == NULL) {
    return TASK_ARCHIVE_AFTER_DAYS;
  }

  if (g_ascii_strcasecmp(value, "immediate") == 0) {
    return TASK_ARCHIVE_IMMEDIATE;
  }

  if (g_ascii_strcasecmp(value, "keep_latest") == 0 ||
      g_ascii_strcasecmp(value, "keep-latest") == 0) {
    return TASK_ARCHIVE_KEEP_LATEST;
  }

  return TASK_ARCHIVE_AFTER_DAYS;
}

static char *
format_datetime(GDateTime *datetime)
{
  if (datetime == NULL) {
    return NULL;
  }

  return g_date_time_format_iso8601(datetime);
}

static GDateTime *
parse_datetime(const char *value)
{
  if (value == NULL || *value == '\0') {
    return NULL;
  }

  return g_date_time_new_from_iso8601(value, NULL);
}

char *
task_storage_get_path(void)
{
  return g_build_filename(g_get_user_data_dir(),
                          "xfce4-floating-pomodoro",
                          "tasks.ini",
                          NULL);
}

gboolean
task_storage_load(TaskStore *store, GError **error)
{
  if (store == NULL) {
    g_set_error(error,
                G_FILE_ERROR,
                G_FILE_ERROR_INVAL,
                "Task store is NULL");
    return FALSE;
  }

  char *path = task_storage_get_path();
  if (!g_file_test(path, G_FILE_TEST_EXISTS)) {
    g_free(path);
    return TRUE;
  }

  GKeyFile *key_file = g_key_file_new();
  if (!g_key_file_load_from_file(key_file, path, G_KEY_FILE_NONE, error)) {
    g_key_file_free(key_file);
    g_free(path);
    return FALSE;
  }

  task_store_clear(store);

  TaskArchiveStrategy strategy = task_store_get_archive_strategy(store);
  char *strategy_value =
      g_key_file_get_string(key_file, "archive", "strategy", NULL);
  strategy.type = archive_strategy_from_string(strategy_value);
  g_free(strategy_value);

  if (g_key_file_has_key(key_file, "archive", "days", NULL)) {
    strategy.days = (guint)g_key_file_get_integer(key_file, "archive", "days", NULL);
  }
  if (g_key_file_has_key(key_file, "archive", "keep_latest", NULL)) {
    strategy.keep_latest =
        (guint)g_key_file_get_integer(key_file, "archive", "keep_latest", NULL);
  }
  task_store_set_archive_strategy(store, strategy);

  gsize group_len = 0;
  gchar **groups = g_key_file_get_groups(key_file, &group_len);
  for (gsize i = 0; i < group_len; i++) {
    const char *group = groups[i];
    if (!g_str_has_prefix(group, "task:")) {
      continue;
    }

    const char *id = group + strlen("task:");
    if (id[0] == '\0') {
      continue;
    }

    char *title = g_key_file_get_string(key_file, group, "title", NULL);
    if (title == NULL || *title == '\0') {
      g_free(title);
      title = g_strdup("Untitled Task");
    }

    char *status_value =
        g_key_file_get_string(key_file, group, "status", NULL);
    TaskStatus status = task_status_from_string(status_value);
    g_free(status_value);

    guint repeat_count = 1;
    if (g_key_file_has_key(key_file, group, "repeat_count", NULL)) {
      gint repeat_value =
          g_key_file_get_integer(key_file, group, "repeat_count", NULL);
      if (repeat_value > 0) {
        repeat_count = (guint)repeat_value;
      }
    }

    char *created_value =
        g_key_file_get_string(key_file, group, "created_at", NULL);
    char *completed_value =
        g_key_file_get_string(key_file, group, "completed_at", NULL);
    char *archived_value =
        g_key_file_get_string(key_file, group, "archived_at", NULL);

    GDateTime *created_at = parse_datetime(created_value);
    GDateTime *completed_at = parse_datetime(completed_value);
    GDateTime *archived_at = parse_datetime(archived_value);

    g_free(created_value);
    g_free(completed_value);
    g_free(archived_value);

    task_store_import(store,
                      id,
                      title,
                      repeat_count,
                      status,
                      created_at,
                      completed_at,
                      archived_at);

    g_free(title);
  }

  g_strfreev(groups);
  g_key_file_free(key_file);
  g_free(path);

  task_store_enforce_single_active(store);

  return TRUE;
}

gboolean
task_storage_save(TaskStore *store, GError **error)
{
  if (store == NULL) {
    g_set_error(error,
                G_FILE_ERROR,
                G_FILE_ERROR_INVAL,
                "Task store is NULL");
    return FALSE;
  }

  char *path = task_storage_get_path();
  char *dir = g_path_get_dirname(path);
  if (g_mkdir_with_parents(dir, 0755) != 0) {
    g_set_error(error,
                G_FILE_ERROR,
                g_file_error_from_errno(errno),
                "Failed to create data directory '%s'",
                dir);
    g_free(dir);
    g_free(path);
    return FALSE;
  }
  g_free(dir);

  GKeyFile *key_file = g_key_file_new();

  TaskArchiveStrategy strategy = task_store_get_archive_strategy(store);
  g_key_file_set_string(key_file,
                        "archive",
                        "strategy",
                        archive_strategy_to_string(strategy.type));
  g_key_file_set_integer(key_file, "archive", "days", (gint)strategy.days);
  g_key_file_set_integer(key_file,
                         "archive",
                         "keep_latest",
                         (gint)strategy.keep_latest);

  const GPtrArray *tasks = task_store_get_tasks(store);
  if (tasks != NULL) {
    for (guint i = 0; i < tasks->len; i++) {
      PomodoroTask *task = g_ptr_array_index((GPtrArray *)tasks, i);
      if (task == NULL) {
        continue;
      }

      const char *id = pomodoro_task_get_id(task);
      if (id == NULL || *id == '\0') {
        continue;
      }

      char *group = g_strdup_printf("task:%s", id);

      g_key_file_set_string(key_file,
                            group,
                            "title",
                            pomodoro_task_get_title(task));
      g_key_file_set_string(key_file,
                            group,
                            "status",
                            task_status_to_string(pomodoro_task_get_status(task)));
      g_key_file_set_integer(key_file,
                             group,
                             "repeat_count",
                             (gint)pomodoro_task_get_repeat_count(task));

      char *created_at = format_datetime(pomodoro_task_get_created_at(task));
      char *completed_at = format_datetime(pomodoro_task_get_completed_at(task));
      char *archived_at = format_datetime(pomodoro_task_get_archived_at(task));

      if (created_at != NULL) {
        g_key_file_set_string(key_file, group, "created_at", created_at);
      }
      if (completed_at != NULL) {
        g_key_file_set_string(key_file, group, "completed_at", completed_at);
      }
      if (archived_at != NULL) {
        g_key_file_set_string(key_file, group, "archived_at", archived_at);
      }

      g_free(created_at);
      g_free(completed_at);
      g_free(archived_at);
      g_free(group);
    }
  }

  gsize length = 0;
  gchar *data = g_key_file_to_data(key_file, &length, error);
  if (data == NULL) {
    g_key_file_free(key_file);
    g_free(path);
    return FALSE;
  }

  gboolean result = g_file_set_contents(path, data, length, error);

  g_free(data);
  g_key_file_free(key_file);
  g_free(path);

  return result;
}
