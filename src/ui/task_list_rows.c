#include "ui/task_list_internal.h"
#include "ui/task_list.h"

#include "focus/focus_guard.h"
#include "storage/task_storage.h"
#include "ui/dialogs.h"

static void on_task_status_clicked(GtkButton *button, gpointer user_data);
static void on_task_archive_clicked(GtkButton *button, gpointer user_data);
static void on_task_restore_clicked(GtkButton *button, gpointer user_data);
static void on_task_delete_clicked(GtkButton *button, gpointer user_data);
static void on_task_row_pressed(GtkGestureClick *gesture,
                                gint n_press,
                                gdouble x,
                                gdouble y,
                                gpointer user_data);

static GtkWidget *
create_task_icon(const char *icon_name)
{
  GtkWidget *image = gtk_image_new_from_icon_name(icon_name);
  gtk_image_set_pixel_size(GTK_IMAGE(image), 20);
  return image;
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
    char *summary = task_list_format_cycle_summary(cycles);
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
    task_list_update_current_summary(controls->state);
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

static void
on_task_row_pressed(GtkGestureClick *gesture,
                    gint n_press,
                    gdouble x,
                    gdouble y,
                    gpointer user_data)
{
  (void)gesture;
  (void)n_press;
  (void)x;
  (void)y;

  TaskRowControls *controls = user_data;
  if (controls == NULL || controls->state == NULL || controls->task == NULL) {
    return;
  }

  if (controls->state->focus_guard == NULL) {
    return;
  }

  focus_guard_select_task(controls->state->focus_guard, controls->task);
}

void
task_list_append_row(AppState *state, GtkWidget *list, PomodoroTask *task)
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

  char *repeat_text = task_list_format_cycle_summary(pomodoro_task_get_repeat_count(task));
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
  if (status == TASK_STATUS_PENDING) {
    status_text = "Pending";
  } else if (status == TASK_STATUS_COMPLETED) {
    status_text = "Complete";
  } else if (status == TASK_STATUS_ARCHIVED) {
    status_text = "Archived";
  }

  GtkWidget *status_button = gtk_button_new_with_label(status_text);
  gtk_widget_add_css_class(status_button, "task-status");
  gtk_widget_add_css_class(status_button, "tag");
  gtk_widget_set_valign(status_button, GTK_ALIGN_CENTER);
  if (status == TASK_STATUS_PENDING) {
    gtk_widget_add_css_class(status_button, "tag-pending");
  } else if (status == TASK_STATUS_COMPLETED) {
    gtk_widget_add_css_class(status_button, "tag-success");
  } else if (status == TASK_STATUS_ARCHIVED) {
    gtk_widget_add_css_class(status_button, "tag-muted");
    gtk_widget_set_sensitive(status_button, FALSE);
  }

  GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
  gtk_widget_set_valign(actions, GTK_ALIGN_CENTER);

  GtkWidget *edit_button = gtk_button_new();
  gtk_widget_add_css_class(edit_button, "icon-button");
  gtk_widget_set_size_request(edit_button, 32, 32);
  GtkWidget *edit_icon = create_task_icon("pomodoro-edit-symbolic");
  gtk_button_set_child(GTK_BUTTON(edit_button), edit_icon);
  gtk_widget_set_tooltip_text(edit_button, "Edit task");

  GtkWidget *archive_button = gtk_button_new();
  gtk_widget_add_css_class(archive_button, "icon-button");
  gtk_widget_set_size_request(archive_button, 32, 32);
  GtkWidget *archive_icon = create_task_icon("pomodoro-archive-symbolic");
  gtk_button_set_child(GTK_BUTTON(archive_button), archive_icon);
  gtk_widget_set_tooltip_text(archive_button, "Archive task");

  GtkWidget *restore_button = gtk_button_new();
  gtk_widget_add_css_class(restore_button, "icon-button");
  gtk_widget_set_size_request(restore_button, 32, 32);
  GtkWidget *restore_icon = create_task_icon("pomodoro-restore-symbolic");
  gtk_button_set_child(GTK_BUTTON(restore_button), restore_icon);
  gtk_widget_set_tooltip_text(restore_button, "Restore task");

  GtkWidget *delete_button = gtk_button_new();
  gtk_widget_add_css_class(delete_button, "icon-button");
  gtk_widget_add_css_class(delete_button, "icon-danger");
  gtk_widget_set_size_request(delete_button, 32, 32);
  GtkWidget *delete_icon = create_task_icon("pomodoro-delete-symbolic");
  gtk_button_set_child(GTK_BUTTON(delete_button), delete_icon);
  gtk_widget_set_tooltip_text(delete_button, "Delete task");

  gtk_box_append(GTK_BOX(actions), edit_button);

  if (status == TASK_STATUS_ARCHIVED) {
    gtk_box_append(GTK_BOX(actions), restore_button);
  } else {
    gtk_box_append(GTK_BOX(actions), archive_button);
  }

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
  g_object_set_data(G_OBJECT(status_button), "task", task);
  g_object_set_data(G_OBJECT(archive_button), "task", task);
  g_object_set_data(G_OBJECT(restore_button), "task", task);
  g_object_set_data(G_OBJECT(delete_button), "task", task);

  GtkGesture *row_click = gtk_gesture_click_new();
  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(row_click), 0);
  g_signal_connect(row_click,
                   "pressed",
                   G_CALLBACK(on_task_row_pressed),
                   controls);
  gtk_widget_add_controller(row, GTK_EVENT_CONTROLLER(row_click));

  g_signal_connect(decrement_button,
                   "clicked",
                   G_CALLBACK(on_task_cycle_decrement),
                   controls);
  g_signal_connect(increment_button,
                   "clicked",
                   G_CALLBACK(on_task_cycle_increment),
                   controls);
  g_signal_connect(status_button,
                   "clicked",
                   G_CALLBACK(on_task_status_clicked),
                   state);
  g_signal_connect(archive_button,
                   "clicked",
                   G_CALLBACK(on_task_archive_clicked),
                   state);
  g_signal_connect(restore_button,
                   "clicked",
                   G_CALLBACK(on_task_restore_clicked),
                   state);
  g_signal_connect(delete_button,
                   "clicked",
                   G_CALLBACK(on_task_delete_clicked),
                   state);
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

  if (status == TASK_STATUS_ACTIVE || status == TASK_STATUS_COMPLETED) {
    task_store_set_pending(state->store, task);
    task_store_apply_archive_policy(state->store);
    task_list_save_store(state);
    task_list_refresh(state);
    return;
  }

  if (status == TASK_STATUS_PENDING) {
    PomodoroTask *active_task = task_store_get_active(state->store);
    const char *body_text =
        active_task != NULL
            ? "Make this task active? The current active task will be moved to pending."
            : "Make this task active?";

    dialogs_show_confirm(
        state,
        "Activate task?",
        body_text,
        task,
        DIALOG_CONFIRM_ACTIVATE_TASK);
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
