#ifndef TASK_STORE_H
#define TASK_STORE_H

#include <glib.h>

typedef enum {
  TASK_STATUS_ACTIVE = 0,
  TASK_STATUS_COMPLETED = 1,
  TASK_STATUS_ARCHIVED = 2
} TaskStatus;

typedef enum {
  TASK_ARCHIVE_AFTER_DAYS = 0,
  TASK_ARCHIVE_IMMEDIATE = 1,
  TASK_ARCHIVE_KEEP_LATEST = 2
} TaskArchiveStrategyType;

typedef struct {
  TaskArchiveStrategyType type;
  guint days;
  guint keep_latest;
} TaskArchiveStrategy;

typedef struct _PomodoroTask PomodoroTask;
typedef struct _TaskStore TaskStore;

TaskStore *task_store_new(void);
void task_store_free(TaskStore *store);
void task_store_clear(TaskStore *store);

const GPtrArray *task_store_get_tasks(TaskStore *store);
PomodoroTask *task_store_add(TaskStore *store, const char *title);
PomodoroTask *task_store_import(TaskStore *store,
                                const char *id,
                                const char *title,
                                TaskStatus status,
                                GDateTime *created_at,
                                GDateTime *completed_at,
                                GDateTime *archived_at);
PomodoroTask *task_store_find_by_id(TaskStore *store, const char *id);

void task_store_complete(TaskStore *store, PomodoroTask *task);
void task_store_reactivate(TaskStore *store, PomodoroTask *task);
void task_store_archive_task(TaskStore *store, PomodoroTask *task);
gboolean task_store_remove(TaskStore *store, PomodoroTask *task);

void task_store_set_archive_strategy(TaskStore *store, TaskArchiveStrategy strategy);
TaskArchiveStrategy task_store_get_archive_strategy(TaskStore *store);
void task_store_apply_archive_policy(TaskStore *store);

const char *pomodoro_task_get_id(const PomodoroTask *task);
const char *pomodoro_task_get_title(const PomodoroTask *task);
void pomodoro_task_set_title(PomodoroTask *task, const char *title);
TaskStatus pomodoro_task_get_status(const PomodoroTask *task);
GDateTime *pomodoro_task_get_created_at(const PomodoroTask *task);
GDateTime *pomodoro_task_get_completed_at(const PomodoroTask *task);
GDateTime *pomodoro_task_get_archived_at(const PomodoroTask *task);

#endif
