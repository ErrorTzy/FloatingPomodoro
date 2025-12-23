#ifndef TASK_STORAGE_H
#define TASK_STORAGE_H

#include <glib.h>

#include "core/task_store.h"

char *task_storage_get_path(void);
gboolean task_storage_load(TaskStore *store, GError **error);
gboolean task_storage_save(TaskStore *store, GError **error);

#endif
