#include "ui/task_list.h"

#include "storage/task_storage.h"
#include "ui/dialogs.h"

typedef struct _TaskRowControls {
  AppState *state;
  PomodoroTask *task;
  GtkWidget *repeat_label;
  GtkWidget *count_label;
  GtkWidget *title_label;
  GtkWidget *title_entry;
  GtkWidget *edit_button;
  gboolean title_edit_active;
  gboolean title_edit_has_focus;
  gint64 title_edit_started_at;
} TaskRowControls;

static void on_task_status_clicked(GtkButton *button, gpointer user_data);
static void on_task_edit_clicked(GtkButton *button, gpointer user_data);
static void on_task_archive_clicked(GtkButton *button, gpointer user_data);
static void on_task_restore_clicked(GtkButton *button, gpointer user_data);
static void on_task_delete_clicked(GtkButton *button, gpointer user_data);

static GtkWidget *
create_task_icon(const char *icon_name)
{
  GtkWidget *image = gtk_image_new_from_icon_name(icon_name);
  gtk_image_set_pixel_size(GTK_IMAGE(image), 20);
  return image;
}

static PomodoroTask *find_active_task(TaskStore *store);
static void update_current_task_summary(AppState *state);
static void append_task_row(AppState *state, GtkWidget *list, PomodoroTask *task);

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

static PomodoroTask *
find_active_task(TaskStore *store)
{
  if (store == NULL) {
    return NULL;
  }

  const GPtrArray *tasks = task_store_get_tasks(store);
  if (tasks == NULL) {
    return NULL;
  }

  for (guint i = 0; i < tasks->len; i++) {
    PomodoroTask *task = g_ptr_array_index((GPtrArray *)tasks, i);
    if (task != NULL && pomodoro_task_get_status(task) == TASK_STATUS_ACTIVE) {
      return task;
    }
  }

  return NULL;
}

static guint
calculate_cycle_minutes(guint cycles)
{
  const guint focus_minutes = 25;
  const guint short_break_minutes = 5;
  const guint long_break_minutes = 15;
  const guint long_break_interval = 4;

  if (cycles < 1) {
    cycles = 1;
  }

  guint total = cycles * focus_minutes;
  guint breaks = cycles;
  guint long_breaks = breaks / long_break_interval;
  guint short_breaks = breaks - long_breaks;

  total += short_breaks * short_break_minutes;
  total += long_breaks * long_break_minutes;

  return total;
}

static char *
format_minutes(guint minutes)
{
  guint hours = minutes / 60;
  guint mins = minutes % 60;

  if (hours == 0) {
    return g_strdup_printf("%um", mins);
  }

  if (mins == 0) {
    return g_strdup_printf("%uh", hours);
  }

  return g_strdup_printf("%uh %um", hours, mins);
}

static char *
format_cycle_summary(guint cycles)
{
  if (cycles < 1) {
    cycles = 1;
  }

  char *duration = format_minutes(calculate_cycle_minutes(cycles));
  char *text = g_strdup_printf("%u cycle%s - %s total",
                               cycles,
                               cycles == 1 ? "" : "s",
                               duration);
  g_free(duration);
  return text;
}

void
task_list_update_repeat_hint(GtkSpinButton *spin, GtkWidget *label)
{
  if (spin == NULL || label == NULL) {
    return;
  }

  guint cycles = (guint)gtk_spin_button_get_value_as_int(spin);
  char *duration = format_minutes(calculate_cycle_minutes(cycles));
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
task_row_controls_free(gpointer data)
{
  TaskRowControls *controls = data;
  if (controls == NULL) {
    return;
  }

  g_free(controls);
}

static void
update_task_cycle_ui(TaskRowControls *controls)
{
  if (controls == NULL || controls->task == NULL) {
    return;
  }

  guint cycles = pomodoro_task_get_repeat_count(controls->task);

  if (controls->count_label != NULL) {
    char *count = g_strdup_printf("%u", cycles);
    gtk_label_set_text(GTK_LABEL(controls->count_label), count);
    g_free(count);
  }

  if (controls->repeat_label != NULL) {
    char *summary = format_cycle_summary(cycles);
    gtk_label_set_text(GTK_LABEL(controls->repeat_label), summary);
    g_free(summary);
  }
}

static void
set_task_cycles(TaskRowControls *controls, guint cycles)
{
  if (controls == NULL || controls->task == NULL) {
    return;
  }

  pomodoro_task_set_repeat_count(controls->task, cycles);
  update_task_cycle_ui(controls);

  if (controls->state != NULL) {
    task_list_save_store(controls->state);
    update_current_task_summary(controls->state);
  }
}

static void
on_task_cycle_decrement(GtkButton *button, gpointer user_data)
{
  (void)button;
  TaskRowControls *controls = user_data;
  if (controls == NULL || controls->task == NULL) {
    return;
  }

  guint cycles = pomodoro_task_get_repeat_count(controls->task);
  if (cycles > 1) {
    set_task_cycles(controls, cycles - 1);
  }
}

static void
on_task_cycle_increment(GtkButton *button, gpointer user_data)
{
  (void)button;
  TaskRowControls *controls = user_data;
  if (controls == NULL || controls->task == NULL) {
    return;
  }

  guint cycles = pomodoro_task_get_repeat_count(controls->task);
  if (cycles < 99) {
    set_task_cycles(controls, cycles + 1);
  }
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
    update_current_task_summary(controls->state);
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

static void
on_task_title_activate(GtkEntry *entry, gpointer user_data)
{
  (void)entry;
  g_debug("Inline task title activated");
  apply_task_title_edit((TaskRowControls *)user_data);
}

static void
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
  if (state == NULL || state->editing_controls == NULL) {
    return;
  }

  TaskRowControls *controls = state->editing_controls;
  if (controls->title_entry == NULL ||
      !gtk_widget_get_visible(controls->title_entry)) {
    return;
  }

  GtkWidget *root = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
  GtkWidget *target = gtk_widget_pick(root, x, y, GTK_PICK_DEFAULT);
  if (target == NULL) {
    return;
  }

  if (target == controls->title_entry ||
      gtk_widget_is_ancestor(target, controls->title_entry)) {
    return;
  }

  if (controls->edit_button != NULL &&
      (target == controls->edit_button ||
       gtk_widget_is_ancestor(target, controls->edit_button))) {
    return;
  }

  g_debug("Window click outside title entry; applying inline edit");
  apply_task_title_edit(controls);
}

static void
update_current_task_summary(AppState *state)
{
  if (state == NULL || state->current_task_label == NULL) {
    return;
  }

  const PomodoroTask *active_task = find_active_task(state->store);

  if (active_task != NULL) {
    gtk_label_set_text(GTK_LABEL(state->current_task_label),
                       pomodoro_task_get_title(active_task));
    if (state->current_task_meta != NULL) {
      char *repeat_text =
          format_cycle_summary(pomodoro_task_get_repeat_count(active_task));
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
                         "Add a task below or reactivate a completed one");
    }
  }
}

static void
append_task_row(AppState *state, GtkWidget *list, PomodoroTask *task)
{
  if (state == NULL || list == NULL || task == NULL) {
    return;
  }

  TaskStatus status = pomodoro_task_get_status(task);

  GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_widget_add_css_class(row, "task-row");

  GtkWidget *text_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
  gtk_widget_set_hexpand(text_box, TRUE);

  GtkWidget *title = gtk_label_new(pomodoro_task_get_title(task));
  gtk_widget_add_css_class(title, "task-item");
  gtk_widget_set_halign(title, GTK_ALIGN_START);
  gtk_widget_set_hexpand(title, TRUE);
  gtk_label_set_wrap(GTK_LABEL(title), TRUE);
  gtk_label_set_xalign(GTK_LABEL(title), 0.0f);

  GtkWidget *title_entry = gtk_entry_new();
  gtk_editable_set_text(GTK_EDITABLE(title_entry), pomodoro_task_get_title(task));
  gtk_widget_add_css_class(title_entry, "task-title-entry");
  gtk_widget_set_hexpand(title_entry, TRUE);
  gtk_widget_set_visible(title_entry, FALSE);

  char *repeat_text = format_cycle_summary(pomodoro_task_get_repeat_count(task));
  GtkWidget *repeat_label = gtk_label_new(repeat_text);
  gtk_widget_add_css_class(repeat_label, "task-meta");
  gtk_widget_set_halign(repeat_label, GTK_ALIGN_START);
  gtk_label_set_xalign(GTK_LABEL(repeat_label), 0.0f);
  gtk_label_set_ellipsize(GTK_LABEL(repeat_label), PANGO_ELLIPSIZE_END);
  g_free(repeat_text);

  GtkWidget *cycle_stepper = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
  gtk_widget_add_css_class(cycle_stepper, "cycle-stepper");
  gtk_widget_set_valign(cycle_stepper, GTK_ALIGN_CENTER);

  GtkWidget *decrement_button = gtk_button_new_with_label("-");
  gtk_widget_add_css_class(decrement_button, "stepper-button");

  GtkWidget *count_label = gtk_label_new("");
  gtk_widget_add_css_class(count_label, "task-cycle-count");
  gtk_widget_set_size_request(count_label, 28, -1);
  gtk_widget_set_halign(count_label, GTK_ALIGN_CENTER);

  GtkWidget *increment_button = gtk_button_new_with_label("+");
  gtk_widget_add_css_class(increment_button, "stepper-button");

  gtk_box_append(GTK_BOX(cycle_stepper), decrement_button);
  gtk_box_append(GTK_BOX(cycle_stepper), count_label);
  gtk_box_append(GTK_BOX(cycle_stepper), increment_button);

  if (status == TASK_STATUS_ARCHIVED) {
    gtk_widget_set_sensitive(cycle_stepper, FALSE);
  }

  const char *status_text = "Active";
  if (status == TASK_STATUS_COMPLETED) {
    status_text = "Complete";
  } else if (status == TASK_STATUS_ARCHIVED) {
    status_text = "Archived";
  }

  GtkWidget *status_button = gtk_button_new_with_label(status_text);
  gtk_widget_add_css_class(status_button, "task-status");
  gtk_widget_add_css_class(status_button, "tag");
  gtk_widget_set_valign(status_button, GTK_ALIGN_CENTER);
  if (status == TASK_STATUS_COMPLETED) {
    gtk_widget_add_css_class(status_button, "tag-success");
  } else if (status == TASK_STATUS_ARCHIVED) {
    gtk_widget_add_css_class(status_button, "tag-muted");
    gtk_widget_set_sensitive(status_button, FALSE);
  }
  g_object_set_data(G_OBJECT(status_button), "task", task);
  g_signal_connect(status_button,
                   "clicked",
                   G_CALLBACK(on_task_status_clicked),
                   state);

  GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
  gtk_widget_set_valign(actions, GTK_ALIGN_CENTER);

  GtkWidget *edit_button = gtk_button_new();
  GtkWidget *edit_icon = create_task_icon("pomodoro-edit-symbolic");
  gtk_button_set_child(GTK_BUTTON(edit_button), edit_icon);
  gtk_widget_add_css_class(edit_button, "icon-button");
  gtk_widget_set_tooltip_text(edit_button, "Edit task");
  g_object_set_data(G_OBJECT(edit_button), "task", task);
  gtk_box_append(GTK_BOX(actions), edit_button);

  if (status == TASK_STATUS_ARCHIVED) {
    GtkWidget *restore_button = gtk_button_new();
    GtkWidget *restore_icon = create_task_icon("pomodoro-restore-symbolic");
    gtk_button_set_child(GTK_BUTTON(restore_button), restore_icon);
    gtk_widget_add_css_class(restore_button, "icon-button");
    gtk_widget_set_tooltip_text(restore_button, "Restore task");
    g_object_set_data(G_OBJECT(restore_button), "task", task);
    g_signal_connect(restore_button,
                     "clicked",
                     G_CALLBACK(on_task_restore_clicked),
                     state);
    gtk_box_append(GTK_BOX(actions), restore_button);
  } else {
    GtkWidget *archive_button = gtk_button_new();
    GtkWidget *archive_icon = create_task_icon("pomodoro-archive-symbolic");
    gtk_button_set_child(GTK_BUTTON(archive_button), archive_icon);
    gtk_widget_add_css_class(archive_button, "icon-button");
    gtk_widget_set_tooltip_text(archive_button, "Archive task");
    g_object_set_data(G_OBJECT(archive_button), "task", task);
    g_signal_connect(archive_button,
                     "clicked",
                     G_CALLBACK(on_task_archive_clicked),
                     state);
    gtk_box_append(GTK_BOX(actions), archive_button);
  }

  GtkWidget *delete_button = gtk_button_new();
  GtkWidget *delete_icon = create_task_icon("pomodoro-delete-symbolic");
  gtk_button_set_child(GTK_BUTTON(delete_button), delete_icon);
  gtk_widget_add_css_class(delete_button, "icon-button");
  gtk_widget_add_css_class(delete_button, "icon-danger");
  gtk_widget_set_tooltip_text(delete_button, "Delete task");
  g_object_set_data(G_OBJECT(delete_button), "task", task);
  g_signal_connect(delete_button,
                   "clicked",
                   G_CALLBACK(on_task_delete_clicked),
                   state);
  gtk_box_append(GTK_BOX(actions), delete_button);

  TaskRowControls *controls = g_new0(TaskRowControls, 1);
  controls->state = state;
  controls->task = task;
  controls->repeat_label = repeat_label;
  controls->count_label = count_label;
  controls->title_label = title;
  controls->title_entry = title_entry;
  controls->edit_button = edit_button;
  controls->title_edit_active = FALSE;
  controls->title_edit_has_focus = FALSE;
  controls->title_edit_started_at = 0;

  g_object_set_data_full(G_OBJECT(row),
                         "task-row-controls",
                         controls,
                         task_row_controls_free);
  g_signal_connect(decrement_button,
                   "clicked",
                   G_CALLBACK(on_task_cycle_decrement),
                   controls);
  g_signal_connect(increment_button,
                   "clicked",
                   G_CALLBACK(on_task_cycle_increment),
                   controls);
  g_signal_connect(edit_button,
                   "clicked",
                   G_CALLBACK(on_task_edit_clicked),
                   controls);
  g_signal_connect(title_entry,
                   "activate",
                   G_CALLBACK(on_task_title_activate),
                   controls);
  g_signal_connect(title_entry,
                   "notify::has-focus",
                   G_CALLBACK(on_task_title_focus_changed),
                   controls);
  update_task_cycle_ui(controls);

  gtk_box_append(GTK_BOX(text_box), title);
  gtk_box_append(GTK_BOX(text_box), title_entry);
  gtk_box_append(GTK_BOX(text_box), repeat_label);
  gtk_box_append(GTK_BOX(row), text_box);
  gtk_box_append(GTK_BOX(row), cycle_stepper);
  gtk_box_append(GTK_BOX(row), status_button);
  gtk_box_append(GTK_BOX(row), actions);

  gtk_list_box_append(GTK_LIST_BOX(list), row);
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
        append_task_row(state, state->task_list, task);
        visible_count++;
      }
    }

    for (guint i = 0; i < tasks->len; i++) {
      PomodoroTask *task = g_ptr_array_index((GPtrArray *)tasks, i);
      if (task != NULL &&
          pomodoro_task_get_status(task) == TASK_STATUS_COMPLETED) {
        append_task_row(state, state->task_list, task);
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
          append_task_row(state, archived_list, task);
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

  update_current_task_summary(state);
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

static void
on_task_status_clicked(GtkButton *button, gpointer user_data)
{
  AppState *state = user_data;
  if (state == NULL || button == NULL) {
    return;
  }

  PomodoroTask *task = g_object_get_data(G_OBJECT(button), "task");
  if (task == NULL) {
    return;
  }

  TaskStatus status = pomodoro_task_get_status(task);

  if (status == TASK_STATUS_ACTIVE) {
    dialogs_show_confirm(state,
                         "Complete task?",
                         "Are you sure you want to complete this task?",
                         task,
                         NULL,
                         FALSE);
    return;
  }

  if (status == TASK_STATUS_COMPLETED) {
    PomodoroTask *active_task = find_active_task(state->store);
    if (active_task == NULL) {
      task_store_reactivate(state->store, task);
      task_store_apply_archive_policy(state->store);
      task_list_save_store(state);
      task_list_refresh(state);
      return;
    }

    dialogs_show_confirm(
        state,
        "Switch active task?",
        "Are you sure you want to complete the current task and set this task to active state?",
        task,
        active_task,
        TRUE);
  }
}

static void
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

static void
on_task_archive_clicked(GtkButton *button, gpointer user_data)
{
  AppState *state = user_data;
  PomodoroTask *task = g_object_get_data(G_OBJECT(button), "task");
  if (state == NULL || task == NULL) {
    return;
  }

  task_store_archive_task(state->store, task);
  task_store_apply_archive_policy(state->store);
  task_list_save_store(state);
  task_list_refresh(state);
}

static void
on_task_restore_clicked(GtkButton *button, gpointer user_data)
{
  AppState *state = user_data;
  PomodoroTask *task = g_object_get_data(G_OBJECT(button), "task");
  if (state == NULL || task == NULL) {
    return;
  }

  task_store_reactivate(state->store, task);
  task_store_apply_archive_policy(state->store);
  task_list_save_store(state);
  task_list_refresh(state);
}

static void
on_task_delete_clicked(GtkButton *button, gpointer user_data)
{
  AppState *state = user_data;
  PomodoroTask *task = g_object_get_data(G_OBJECT(button), "task");
  if (state == NULL || task == NULL) {
    return;
  }

  task_store_remove(state->store, task);
  task_list_save_store(state);
  task_list_refresh(state);
}
