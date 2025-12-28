#include "ui/task_list.h"
#include "ui/task_list_internal.h"

#include "storage/task_storage.h"
#include "ui/dialogs.h"
#include "ui/main_window.h"

void
task_list_save_store(AppState *state)
{
  if (state == NULL || state->store == NULL) {
    return;
  }

  GError *error = NULL;
  if (!task_storage_save(state->store, &error)) {
    g_warning("Failed to save tasks: %s", error ? error->message : "unknown error");
    g_clear_error(&error);
  }
}

void
task_list_update_repeat_hint(GtkSpinButton *spin, GtkWidget *label)
{
  if (spin == NULL || label == NULL) {
    return;
  }

  guint cycles = (guint)gtk_spin_button_get_value_as_int(spin);
  char *duration = task_list_format_minutes(task_list_calculate_cycle_minutes(cycles));
  char *text = g_strdup_printf("Estimated total (focus + breaks): %s", duration);
  gtk_label_set_text(GTK_LABEL(label), text);
  g_free(duration);
  g_free(text);
}

void
task_list_on_repeat_spin_changed(GtkSpinButton *spin, gpointer user_data)
{
  task_list_update_repeat_hint(spin, GTK_WIDGET(user_data));
}

static void
clear_list(GtkWidget *list)
{
  GtkWidget *row = gtk_widget_get_first_child(list);
  while (row != NULL) {
    GtkWidget *next = gtk_widget_get_next_sibling(row);
    gtk_list_box_remove(GTK_LIST_BOX(list), row);
    row = next;
  }
}

void
task_list_refresh(AppState *state)
{
  if (state == NULL || state->task_list == NULL) {
    return;
  }

  clear_list(state->task_list);

  const GPtrArray *tasks = task_store_get_tasks(state->store);
  guint visible_count = 0;
  guint archived_count = 0;

  if (tasks != NULL) {
    for (guint i = 0; i < tasks->len; i++) {
      PomodoroTask *task = g_ptr_array_index((GPtrArray *)tasks, i);
      if (task != NULL && pomodoro_task_get_status(task) == TASK_STATUS_ACTIVE) {
        task_list_append_row(state, state->task_list, task);
        visible_count++;
      }
    }

    for (guint i = 0; i < tasks->len; i++) {
      PomodoroTask *task = g_ptr_array_index((GPtrArray *)tasks, i);
      if (task != NULL &&
          pomodoro_task_get_status(task) == TASK_STATUS_PENDING) {
        task_list_append_row(state, state->task_list, task);
        visible_count++;
      }
    }

    for (guint i = 0; i < tasks->len; i++) {
      PomodoroTask *task = g_ptr_array_index((GPtrArray *)tasks, i);
      if (task != NULL &&
          pomodoro_task_get_status(task) == TASK_STATUS_COMPLETED) {
        task_list_append_row(state, state->task_list, task);
        visible_count++;
      }
    }
  }

  GtkWidget *archived_list = NULL;
  GtkWidget *archived_empty = NULL;
  if (dialogs_get_archived_targets(state, &archived_list, &archived_empty)) {
    clear_list(archived_list);

    if (tasks != NULL) {
      for (guint i = 0; i < tasks->len; i++) {
        PomodoroTask *task = g_ptr_array_index((GPtrArray *)tasks, i);
        if (task != NULL &&
            pomodoro_task_get_status(task) == TASK_STATUS_ARCHIVED) {
          task_list_append_row(state, archived_list, task);
          archived_count++;
        }
      }
    }

    if (archived_empty != NULL) {
      gtk_widget_set_visible(archived_empty, archived_count == 0);
    }
  }

  if (state->task_empty_label != NULL) {
    gtk_widget_set_visible(state->task_empty_label, visible_count == 0);
  }

  task_list_update_current_summary(state);
  main_window_update_timer_ui(state);
}

static void
handle_add_task(AppState *state)
{
  if (state == NULL || state->task_entry == NULL ||
      state->task_repeat_spin == NULL) {
    return;
  }

  const char *text =
      gtk_editable_get_text(GTK_EDITABLE(state->task_entry));
  if (text == NULL) {
    return;
  }

  char *trimmed = g_strstrip(g_strdup(text));
  if (trimmed[0] == '\0') {
    g_free(trimmed);
    return;
  }

  guint repeat_count =
      (guint)gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(state->task_repeat_spin));
  task_store_add(state->store, trimmed, repeat_count);
  task_store_apply_archive_policy(state->store);
  task_list_save_store(state);

  gtk_editable_set_text(GTK_EDITABLE(state->task_entry), "");

  task_list_refresh(state);
  g_free(trimmed);
}

void
task_list_on_add_clicked(GtkButton *button, gpointer user_data)
{
  (void)button;
  handle_add_task((AppState *)user_data);
}

void
task_list_on_entry_activate(GtkEntry *entry, gpointer user_data)
{
  (void)entry;
  handle_add_task((AppState *)user_data);
}
