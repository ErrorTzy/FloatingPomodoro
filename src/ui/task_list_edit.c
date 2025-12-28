#include "ui/task_list_internal.h"
#include "ui/task_list.h"

#include "core/task_store.h"
#include "focus/focus_guard.h"

static TaskRowControls *
task_row_controls_from_widget(GtkWidget *widget)
{
  GtkWidget *current = widget;
  while (current != NULL) {
    TaskRowControls *controls =
        g_object_get_data(G_OBJECT(current), "task-row-controls");
    if (controls != NULL) {
      return controls;
    }
    current = gtk_widget_get_parent(current);
  }
  return NULL;
}

static gboolean
focus_title_entry_idle(gpointer data)
{
  GtkWidget *entry = data;
  if (entry != NULL && gtk_widget_get_visible(entry)) {
    gtk_widget_grab_focus(entry);
  }
  return G_SOURCE_REMOVE;
}

static void
set_editing_controls(AppState *state, TaskRowControls *controls)
{
  if (state == NULL) {
    return;
  }

  state->editing_controls = controls;
}

static void
apply_task_title_edit(TaskRowControls *controls)
{
  if (controls == NULL || controls->task == NULL ||
      controls->title_entry == NULL || controls->title_label == NULL) {
    g_warning("apply_task_title_edit: missing controls or widgets");
    return;
  }

  if (!gtk_widget_get_visible(controls->title_entry)) {
    g_debug("apply_task_title_edit: entry not visible; skipping");
    return;
  }

  const char *text = gtk_editable_get_text(GTK_EDITABLE(controls->title_entry));
  if (text == NULL) {
    text = "";
  }

  char *trimmed = g_strstrip(g_strdup(text));
  if (trimmed[0] == '\0') {
    g_free(trimmed);
    trimmed = g_strdup(pomodoro_task_get_title(controls->task));
  }

  const char *current = pomodoro_task_get_title(controls->task);
  if (g_strcmp0(current, trimmed) != 0) {
    g_info("Updating task title to '%s'", trimmed);
    pomodoro_task_set_title(controls->task, trimmed);
  } else {
    g_debug("Task title unchanged");
  }

  gtk_label_set_text(GTK_LABEL(controls->title_label), trimmed);
  gtk_editable_set_text(GTK_EDITABLE(controls->title_entry), trimmed);

  gtk_widget_set_visible(controls->title_entry, FALSE);
  gtk_widget_set_visible(controls->title_label, TRUE);
  controls->title_edit_active = FALSE;
  controls->title_edit_has_focus = FALSE;
  controls->title_edit_started_at = 0;

  g_free(trimmed);

  if (controls->state != NULL) {
    if (controls->state->editing_controls == controls) {
      set_editing_controls(controls->state, NULL);
    }
    task_list_save_store(controls->state);
    task_list_update_current_summary(controls->state);
  }
}

static void
start_task_title_edit(TaskRowControls *controls)
{
  if (controls == NULL || controls->task == NULL ||
      controls->title_entry == NULL || controls->title_label == NULL) {
    g_warning("start_task_title_edit: missing controls or widgets");
    return;
  }

  if (controls->state != NULL &&
      controls->state->editing_controls != NULL &&
      controls->state->editing_controls != controls) {
    apply_task_title_edit(controls->state->editing_controls);
  }

  g_info("Entering inline edit for task '%s'",
         pomodoro_task_get_title(controls->task));
  if (controls->state != NULL) {
    set_editing_controls(controls->state, controls);
  }
  controls->title_edit_active = TRUE;
  controls->title_edit_has_focus = FALSE;
  controls->title_edit_started_at = g_get_monotonic_time();
  gtk_editable_set_text(GTK_EDITABLE(controls->title_entry),
                        pomodoro_task_get_title(controls->task));
  gtk_widget_set_visible(controls->title_label, FALSE);
  gtk_widget_set_visible(controls->title_entry, TRUE);
  gtk_widget_grab_focus(controls->title_entry);
  gtk_editable_set_position(GTK_EDITABLE(controls->title_entry), -1);
  g_idle_add(focus_title_entry_idle, controls->title_entry);
}

void
on_task_edit_clicked(GtkButton *button, gpointer user_data)
{
  (void)button;
  TaskRowControls *controls = user_data;
  if (controls == NULL || controls->task == NULL) {
    g_warning("Edit clicked but task controls are missing");
    return;
  }

  g_debug("Edit icon clicked for task '%s'",
          pomodoro_task_get_title(controls->task));
  if (controls->title_entry != NULL &&
      gtk_widget_get_visible(controls->title_entry)) {
    g_debug("Edit already active; applying inline edit");
    apply_task_title_edit(controls);
  } else {
    g_debug("Starting inline edit");
    start_task_title_edit(controls);
  }
}

void
on_task_title_activate(GtkEntry *entry, gpointer user_data)
{
  (void)entry;
  g_debug("Inline task title activated");
  apply_task_title_edit((TaskRowControls *)user_data);
}

void
on_task_title_focus_changed(GObject *object,
                            GParamSpec *pspec,
                            gpointer user_data)
{
  (void)pspec;
  GtkWidget *entry = GTK_WIDGET(object);
  TaskRowControls *controls = user_data;
  if (gtk_widget_has_focus(entry)) {
    if (controls != NULL) {
      controls->title_edit_has_focus = TRUE;
    }
    g_debug("Inline task title gained focus");
    return;
  }

  if (controls == NULL || !controls->title_edit_active) {
    return;
  }

  if (!controls->title_edit_has_focus) {
    g_debug("Inline task title lost focus before gaining focus; ignoring");
    return;
  }

  if (controls->title_edit_started_at > 0) {
    gint64 elapsed = g_get_monotonic_time() - controls->title_edit_started_at;
    if (elapsed < 250000) {
      g_debug("Inline task title lost focus too quickly; ignoring");
      return;
    }
  }

  g_debug("Inline task title lost focus");
  apply_task_title_edit(controls);
}

void
task_list_on_window_pressed(GtkGestureClick *gesture,
                            gint n_press,
                            gdouble x,
                            gdouble y,
                            gpointer user_data)
{
  (void)n_press;
  AppState *state = user_data;
  if (state == NULL) {
    return;
  }

  GtkWidget *root = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
  GtkWidget *target = gtk_widget_pick(root, x, y, GTK_PICK_DEFAULT);
  if (target == NULL) {
    return;
  }

  TaskRowControls *row_controls = task_row_controls_from_widget(target);
  gboolean clicked_task_row = row_controls != NULL;

  TaskRowControls *controls = state->editing_controls;
  if (controls != NULL &&
      controls->title_entry != NULL &&
      gtk_widget_get_visible(controls->title_entry)) {
    if (target != controls->title_entry &&
        !gtk_widget_is_ancestor(target, controls->title_entry) &&
        (controls->edit_button == NULL ||
         (target != controls->edit_button &&
          !gtk_widget_is_ancestor(target, controls->edit_button)))) {
      g_debug("Window click outside title entry; applying inline edit");
      apply_task_title_edit(controls);
    }
  }

  if (!clicked_task_row && state->focus_guard != NULL) {
    focus_guard_select_global(state->focus_guard);
  }
}

void
task_list_update_current_summary(AppState *state)
{
  if (state == NULL || state->current_task_label == NULL) {
    return;
  }

  const PomodoroTask *active_task = task_store_get_active(state->store);

  if (active_task != NULL) {
    gtk_label_set_text(GTK_LABEL(state->current_task_label),
                       pomodoro_task_get_title(active_task));
    if (state->current_task_meta != NULL) {
      char *repeat_text =
          task_list_format_cycle_summary(pomodoro_task_get_repeat_count(active_task));
      char *meta_text =
          g_strdup_printf("%s. Ready for the next focus session", repeat_text);
      gtk_label_set_text(GTK_LABEL(state->current_task_meta), meta_text);
      g_free(repeat_text);
      g_free(meta_text);
    }
  } else {
    gtk_label_set_text(GTK_LABEL(state->current_task_label), "No active task");
    if (state->current_task_meta != NULL) {
      gtk_label_set_text(GTK_LABEL(state->current_task_meta),
                         "Add a task below or activate a pending one");
    }
  }
}
