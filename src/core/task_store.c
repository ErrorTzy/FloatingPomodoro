#include "core/task_store.h"

#include <string.h>

struct _PomodoroTask {
  char *id;
  char *title;
  TaskStatus status;
  GDateTime *created_at;
  GDateTime *completed_at;
  GDateTime *archived_at;
};

struct _TaskStore {
  GPtrArray *tasks;
  TaskArchiveStrategy archive;
};

static void
pomodoro_task_free(PomodoroTask *task)
{
  if (task == NULL) {
    return;
  }

  g_free(task->id);
  g_free(task->title);
  if (task->created_at != NULL) {
    g_date_time_unref(task->created_at);
  }
  if (task->completed_at != NULL) {
    g_date_time_unref(task->completed_at);
  }
  if (task->archived_at != NULL) {
    g_date_time_unref(task->archived_at);
  }
  g_free(task);
}

static TaskArchiveStrategy
normalize_archive_strategy(TaskArchiveStrategy strategy)
{
  if (strategy.days == 0) {
    strategy.days = 1;
  }
  if (strategy.keep_latest == 0) {
    strategy.keep_latest = 1;
  }
  return strategy;
}

TaskStore *
task_store_new(void)
{
  TaskStore *store = g_new0(TaskStore, 1);
  store->tasks = g_ptr_array_new_with_free_func((GDestroyNotify)pomodoro_task_free);
  store->archive.type = TASK_ARCHIVE_AFTER_DAYS;
  store->archive.days = 3;
  store->archive.keep_latest = 5;
  return store;
}

void
task_store_free(TaskStore *store)
{
  if (store == NULL) {
    return;
  }

  g_ptr_array_free(store->tasks, TRUE);
  g_free(store);
}

void
task_store_clear(TaskStore *store)
{
  if (store == NULL) {
    return;
  }

  g_ptr_array_free(store->tasks, TRUE);
  store->tasks = g_ptr_array_new_with_free_func((GDestroyNotify)pomodoro_task_free);
}

const GPtrArray *
task_store_get_tasks(TaskStore *store)
{
  return store ? store->tasks : NULL;
}

PomodoroTask *
task_store_add(TaskStore *store, const char *title)
{
  if (store == NULL || title == NULL || *title == '\0') {
    return NULL;
  }

  PomodoroTask *task = g_new0(PomodoroTask, 1);
  task->id = g_uuid_string_random();
  task->title = g_strdup(title);
  task->status = TASK_STATUS_ACTIVE;
  task->created_at = g_date_time_new_now_local();

  g_ptr_array_add(store->tasks, task);
  return task;
}

PomodoroTask *
task_store_import(TaskStore *store,
                  const char *id,
                  const char *title,
                  TaskStatus status,
                  GDateTime *created_at,
                  GDateTime *completed_at,
                  GDateTime *archived_at)
{
  if (store == NULL || id == NULL || *id == '\0' || title == NULL) {
    if (created_at != NULL) {
      g_date_time_unref(created_at);
    }
    if (completed_at != NULL) {
      g_date_time_unref(completed_at);
    }
    if (archived_at != NULL) {
      g_date_time_unref(archived_at);
    }
    return NULL;
  }

  PomodoroTask *task = g_new0(PomodoroTask, 1);
  task->id = g_strdup(id);
  task->title = g_strdup(title);
  task->status = status;
  task->created_at = created_at ? created_at : g_date_time_new_now_local();
  task->completed_at = completed_at;
  task->archived_at = archived_at;

  g_ptr_array_add(store->tasks, task);
  return task;
}

PomodoroTask *
task_store_find_by_id(TaskStore *store, const char *id)
{
  if (store == NULL || id == NULL) {
    return NULL;
  }

  for (guint i = 0; i < store->tasks->len; i++) {
    PomodoroTask *task = g_ptr_array_index(store->tasks, i);
    if (task != NULL && g_strcmp0(task->id, id) == 0) {
      return task;
    }
  }
  return NULL;
}

void
task_store_complete(TaskStore *store, PomodoroTask *task)
{
  if (store == NULL || task == NULL) {
    return;
  }

  if (task->status == TASK_STATUS_COMPLETED) {
    return;
  }

  task->status = TASK_STATUS_COMPLETED;
  if (task->completed_at != NULL) {
    g_date_time_unref(task->completed_at);
  }
  task->completed_at = g_date_time_new_now_local();
}

void
task_store_reactivate(TaskStore *store, PomodoroTask *task)
{
  if (store == NULL || task == NULL) {
    return;
  }

  task->status = TASK_STATUS_ACTIVE;
  if (task->completed_at != NULL) {
    g_date_time_unref(task->completed_at);
    task->completed_at = NULL;
  }
  if (task->archived_at != NULL) {
    g_date_time_unref(task->archived_at);
    task->archived_at = NULL;
  }
}

void
task_store_archive_task(TaskStore *store, PomodoroTask *task)
{
  if (store == NULL || task == NULL) {
    return;
  }

  if (task->status == TASK_STATUS_ARCHIVED) {
    return;
  }

  task->status = TASK_STATUS_ARCHIVED;
  if (task->archived_at != NULL) {
    g_date_time_unref(task->archived_at);
  }
  task->archived_at = g_date_time_new_now_local();
}

void
task_store_set_archive_strategy(TaskStore *store, TaskArchiveStrategy strategy)
{
  if (store == NULL) {
    return;
  }

  store->archive = normalize_archive_strategy(strategy);
}

TaskArchiveStrategy
task_store_get_archive_strategy(TaskStore *store)
{
  TaskArchiveStrategy fallback = {
      .type = TASK_ARCHIVE_AFTER_DAYS,
      .days = 3,
      .keep_latest = 5};

  if (store == NULL) {
    return fallback;
  }

  return store->archive;
}

static gint
compare_completed_desc(gconstpointer a, gconstpointer b)
{
  const PomodoroTask *task_a = a;
  const PomodoroTask *task_b = b;

  if (task_a == NULL && task_b == NULL) {
    return 0;
  }
  if (task_a == NULL) {
    return 1;
  }
  if (task_b == NULL) {
    return -1;
  }

  if (task_a->completed_at == NULL && task_b->completed_at == NULL) {
    return 0;
  }
  if (task_a->completed_at == NULL) {
    return 1;
  }
  if (task_b->completed_at == NULL) {
    return -1;
  }

  return -g_date_time_compare(task_a->completed_at, task_b->completed_at);
}

void
task_store_apply_archive_policy(TaskStore *store)
{
  if (store == NULL) {
    return;
  }

  TaskArchiveStrategy strategy = normalize_archive_strategy(store->archive);
  store->archive = strategy;

  if (strategy.type == TASK_ARCHIVE_IMMEDIATE) {
    for (guint i = 0; i < store->tasks->len; i++) {
      PomodoroTask *task = g_ptr_array_index(store->tasks, i);
      if (task != NULL && task->status == TASK_STATUS_COMPLETED) {
        task_store_archive_task(store, task);
      }
    }
    return;
  }

  if (strategy.type == TASK_ARCHIVE_AFTER_DAYS) {
    GDateTime *now = g_date_time_new_now_local();
    GDateTime *cutoff = g_date_time_add_days(now, -(gint)strategy.days);

    for (guint i = 0; i < store->tasks->len; i++) {
      PomodoroTask *task = g_ptr_array_index(store->tasks, i);
      if (task == NULL || task->status != TASK_STATUS_COMPLETED ||
          task->completed_at == NULL) {
        continue;
      }

      if (g_date_time_compare(task->completed_at, cutoff) < 0) {
        task_store_archive_task(store, task);
      }
    }

    g_date_time_unref(cutoff);
    g_date_time_unref(now);
    return;
  }

  if (strategy.type == TASK_ARCHIVE_KEEP_LATEST) {
    GPtrArray *completed = g_ptr_array_new();
    for (guint i = 0; i < store->tasks->len; i++) {
      PomodoroTask *task = g_ptr_array_index(store->tasks, i);
      if (task != NULL && task->status == TASK_STATUS_COMPLETED) {
        g_ptr_array_add(completed, task);
      }
    }

    g_ptr_array_sort(completed, compare_completed_desc);

    for (guint i = strategy.keep_latest; i < completed->len; i++) {
      PomodoroTask *task = g_ptr_array_index(completed, i);
      task_store_archive_task(store, task);
    }

    g_ptr_array_free(completed, TRUE);
  }
}

const char *
pomodoro_task_get_id(const PomodoroTask *task)
{
  return task ? task->id : NULL;
}

const char *
pomodoro_task_get_title(const PomodoroTask *task)
{
  return task ? task->title : NULL;
}

TaskStatus
pomodoro_task_get_status(const PomodoroTask *task)
{
  return task ? task->status : TASK_STATUS_ACTIVE;
}

GDateTime *
pomodoro_task_get_created_at(const PomodoroTask *task)
{
  return task ? task->created_at : NULL;
}

GDateTime *
pomodoro_task_get_completed_at(const PomodoroTask *task)
{
  return task ? task->completed_at : NULL;
}

GDateTime *
pomodoro_task_get_archived_at(const PomodoroTask *task)
{
  return task ? task->archived_at : NULL;
}
