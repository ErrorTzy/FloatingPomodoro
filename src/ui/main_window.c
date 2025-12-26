#include "ui/main_window.h"

#include "app/app_state.h"
#include "core/task_store.h"
#include "storage/task_storage.h"
#include "ui/dialogs.h"
#include "ui/task_list.h"
#include "config.h"

void
main_window_present(GtkApplication *app)
{
  GtkWidget *window = gtk_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(window), APP_NAME);
  gtk_window_set_default_size(GTK_WINDOW(window), 880, 560);
  gtk_widget_add_css_class(window, "app-window");

  TaskStore *store = task_store_new();
  GError *error = NULL;
  if (!task_storage_load(store, &error)) {
    g_warning("Failed to load tasks: %s", error ? error->message : "unknown error");
    g_clear_error(&error);
  }
  task_store_apply_archive_policy(store);

  AppState *state = app_state_create(GTK_WINDOW(window), store);
  g_object_set_data_full(G_OBJECT(window), "app-state", state, app_state_free);

  GtkGesture *window_click = gtk_gesture_click_new();
  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(window_click), 0);
  gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(window_click),
                                             GTK_PHASE_CAPTURE);
  g_signal_connect(window_click,
                   "pressed",
                   G_CALLBACK(task_list_on_window_pressed),
                   state);
  gtk_widget_add_controller(window, GTK_EVENT_CONTROLLER(window_click));

  GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
  gtk_widget_add_css_class(root, "app-root");

  GtkWidget *header = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
  gtk_widget_add_css_class(header, "app-header");

  GtkWidget *title = gtk_label_new(APP_NAME);
  gtk_widget_add_css_class(title, "app-title");
  gtk_widget_set_halign(title, GTK_ALIGN_START);

  GtkWidget *subtitle = gtk_label_new("Task persistence is live. Timer controls arrive next.");
  gtk_widget_add_css_class(subtitle, "app-subtitle");
  gtk_widget_set_halign(subtitle, GTK_ALIGN_START);
  gtk_label_set_wrap(GTK_LABEL(subtitle), TRUE);

  gtk_box_append(GTK_BOX(header), title);
  gtk_box_append(GTK_BOX(header), subtitle);
  gtk_box_append(GTK_BOX(root), header);

  GtkWidget *action_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  gtk_widget_set_halign(action_row, GTK_ALIGN_START);

  GtkWidget *settings_button = gtk_button_new_with_label("Archive Settings");
  gtk_widget_add_css_class(settings_button, "btn-secondary");
  gtk_widget_add_css_class(settings_button, "btn-compact");
  g_signal_connect(settings_button,
                   "clicked",
                   G_CALLBACK(dialogs_on_show_settings_clicked),
                   state);

  GtkWidget *archived_button = gtk_button_new_with_label("Archived Tasks");
  gtk_widget_add_css_class(archived_button, "btn-secondary");
  gtk_widget_add_css_class(archived_button, "btn-compact");
  g_signal_connect(archived_button,
                   "clicked",
                   G_CALLBACK(dialogs_on_show_archived_clicked),
                   state);

  gtk_box_append(GTK_BOX(action_row), settings_button);
  gtk_box_append(GTK_BOX(action_row), archived_button);
  gtk_box_append(GTK_BOX(root), action_row);

  GtkWidget *hero = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 18);
  gtk_widget_set_hexpand(hero, TRUE);

  GtkWidget *timer_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_add_css_class(timer_card, "card");
  gtk_widget_set_hexpand(timer_card, TRUE);

  GtkWidget *timer_title = gtk_label_new("Focus Session");
  gtk_widget_add_css_class(timer_title, "card-title");
  gtk_widget_set_halign(timer_title, GTK_ALIGN_START);

  GtkWidget *timer_value = gtk_label_new("25:00");
  gtk_widget_add_css_class(timer_value, "timer-value");
  gtk_widget_set_halign(timer_value, GTK_ALIGN_START);

  GtkWidget *timer_pill = gtk_label_new("Next: short break");
  gtk_widget_add_css_class(timer_pill, "pill");
  gtk_widget_set_halign(timer_pill, GTK_ALIGN_START);

  GtkWidget *timer_actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  gtk_widget_set_halign(timer_actions, GTK_ALIGN_START);

  GtkWidget *start_button = gtk_button_new_with_label("Start Focus");
  gtk_widget_add_css_class(start_button, "btn-primary");

  GtkWidget *skip_button = gtk_button_new_with_label("Skip");
  gtk_widget_add_css_class(skip_button, "btn-secondary");

  gtk_box_append(GTK_BOX(timer_actions), start_button);
  gtk_box_append(GTK_BOX(timer_actions), skip_button);

  gtk_box_append(GTK_BOX(timer_card), timer_title);
  gtk_box_append(GTK_BOX(timer_card), timer_value);
  gtk_box_append(GTK_BOX(timer_card), timer_pill);
  gtk_box_append(GTK_BOX(timer_card), timer_actions);

  GtkWidget *task_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_widget_add_css_class(task_card, "card");
  gtk_widget_set_hexpand(task_card, TRUE);

  GtkWidget *task_title = gtk_label_new("Current Task");
  gtk_widget_add_css_class(task_title, "card-title");
  gtk_widget_set_halign(task_title, GTK_ALIGN_START);

  GtkWidget *task_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_halign(task_row, GTK_ALIGN_START);

  GtkWidget *task_label = gtk_label_new("No active task");
  gtk_widget_add_css_class(task_label, "task-item");
  state->current_task_label = task_label;

  GtkWidget *task_tag = gtk_label_new("Ready");
  gtk_widget_add_css_class(task_tag, "tag");

  gtk_box_append(GTK_BOX(task_row), task_label);
  gtk_box_append(GTK_BOX(task_row), task_tag);

  GtkWidget *task_meta = gtk_label_new("Add a task below or reactivate a completed one");
  gtk_widget_add_css_class(task_meta, "task-meta");
  gtk_widget_set_halign(task_meta, GTK_ALIGN_START);
  state->current_task_meta = task_meta;

  GtkWidget *stats_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 18);
  gtk_widget_set_halign(stats_row, GTK_ALIGN_START);

  GtkWidget *stat_block_left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  GtkWidget *stat_value_left = gtk_label_new("00:00");
  gtk_widget_add_css_class(stat_value_left, "stat-value");
  GtkWidget *stat_label_left = gtk_label_new("Focus time");
  gtk_widget_add_css_class(stat_label_left, "stat-label");
  gtk_box_append(GTK_BOX(stat_block_left), stat_value_left);
  gtk_box_append(GTK_BOX(stat_block_left), stat_label_left);

  GtkWidget *stat_block_right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  GtkWidget *stat_value_right = gtk_label_new("0");
  gtk_widget_add_css_class(stat_value_right, "stat-value");
  GtkWidget *stat_label_right = gtk_label_new("Breaks");
  gtk_widget_add_css_class(stat_label_right, "stat-label");
  gtk_box_append(GTK_BOX(stat_block_right), stat_value_right);
  gtk_box_append(GTK_BOX(stat_block_right), stat_label_right);

  gtk_box_append(GTK_BOX(stats_row), stat_block_left);
  gtk_box_append(GTK_BOX(stats_row), stat_block_right);

  gtk_box_append(GTK_BOX(task_card), task_title);
  gtk_box_append(GTK_BOX(task_card), task_row);
  gtk_box_append(GTK_BOX(task_card), task_meta);
  gtk_box_append(GTK_BOX(task_card), stats_row);

  gtk_box_append(GTK_BOX(hero), timer_card);
  gtk_box_append(GTK_BOX(hero), task_card);
  gtk_box_append(GTK_BOX(root), hero);

  GtkWidget *task_section = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 18);
  gtk_widget_set_hexpand(task_section, TRUE);

  GtkWidget *tasks_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_add_css_class(tasks_card, "card");
  gtk_widget_set_hexpand(tasks_card, TRUE);

  GtkWidget *tasks_title = gtk_label_new("Tasks");
  gtk_widget_add_css_class(tasks_title, "card-title");
  gtk_widget_set_halign(tasks_title, GTK_ALIGN_START);

  GtkWidget *task_input_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_set_hexpand(task_input_box, TRUE);

  GtkWidget *task_input_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  gtk_widget_set_hexpand(task_input_row, TRUE);
  gtk_widget_add_css_class(task_input_row, "task-input-row");

  GtkWidget *task_entry = gtk_entry_new();
  gtk_widget_set_hexpand(task_entry, TRUE);
  gtk_widget_set_valign(task_entry, GTK_ALIGN_CENTER);
  gtk_entry_set_placeholder_text(GTK_ENTRY(task_entry),
                                 "Add a task for the next focus block");
  gtk_widget_add_css_class(task_entry, "task-entry");
  g_signal_connect(task_entry,
                   "activate",
                   G_CALLBACK(task_list_on_entry_activate),
                   state);
  state->task_entry = task_entry;

  GtkWidget *task_add_button = gtk_button_new_with_label("Add");
  gtk_widget_add_css_class(task_add_button, "btn-primary");
  gtk_widget_add_css_class(task_add_button, "task-add");
  gtk_widget_set_valign(task_add_button, GTK_ALIGN_CENTER);
  g_signal_connect(task_add_button,
                   "clicked",
                   G_CALLBACK(task_list_on_add_clicked),
                   state);

  gtk_box_append(GTK_BOX(task_input_row), task_entry);
  gtk_box_append(GTK_BOX(task_input_row), task_add_button);

  GtkWidget *task_meta_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  gtk_widget_set_hexpand(task_meta_row, TRUE);
  gtk_widget_add_css_class(task_meta_row, "task-input-meta");

  GtkWidget *repeat_group = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_halign(repeat_group, GTK_ALIGN_START);
  gtk_widget_add_css_class(repeat_group, "task-repeat-group");

  GtkWidget *repeat_label = gtk_label_new("Cycles");
  gtk_widget_add_css_class(repeat_label, "task-meta");

  GtkAdjustment *repeat_adjustment = gtk_adjustment_new(1, 1, 99, 1, 5, 0);
  GtkWidget *repeat_spin = gtk_spin_button_new(repeat_adjustment, 1, 0);
  gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(repeat_spin), TRUE);
  gtk_widget_add_css_class(repeat_spin, "task-spin");
  gtk_widget_set_size_request(repeat_spin, 72, -1);
  state->task_repeat_spin = repeat_spin;

  GtkWidget *repeat_hint = gtk_label_new("");
  gtk_widget_add_css_class(repeat_hint, "task-meta");
  gtk_widget_set_hexpand(repeat_hint, TRUE);
  gtk_widget_set_halign(repeat_hint, GTK_ALIGN_END);
  gtk_label_set_xalign(GTK_LABEL(repeat_hint), 1.0f);
  gtk_label_set_ellipsize(GTK_LABEL(repeat_hint), PANGO_ELLIPSIZE_END);
  gtk_widget_set_tooltip_text(
      repeat_hint,
      "Assumes each cycle is 25m focus + 5m break; every 4th break is 15m.");
  state->task_repeat_hint = repeat_hint;

  g_signal_connect(repeat_spin,
                   "value-changed",
                   G_CALLBACK(task_list_on_repeat_spin_changed),
                   repeat_hint);
  task_list_update_repeat_hint(GTK_SPIN_BUTTON(repeat_spin), repeat_hint);

  gtk_box_append(GTK_BOX(repeat_group), repeat_label);
  gtk_box_append(GTK_BOX(repeat_group), repeat_spin);

  gtk_box_append(GTK_BOX(task_meta_row), repeat_group);
  gtk_box_append(GTK_BOX(task_meta_row), repeat_hint);

  gtk_box_append(GTK_BOX(task_input_box), task_input_row);
  gtk_box_append(GTK_BOX(task_input_box), task_meta_row);

  GtkWidget *task_list = gtk_list_box_new();
  gtk_widget_add_css_class(task_list, "task-list");
  gtk_list_box_set_selection_mode(GTK_LIST_BOX(task_list), GTK_SELECTION_NONE);
  state->task_list = task_list;

  GtkWidget *task_scroller = gtk_scrolled_window_new();
  gtk_widget_add_css_class(task_scroller, "task-scroller");
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(task_scroller),
                                 GTK_POLICY_NEVER,
                                 GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(task_scroller), task_list);
  gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(task_scroller),
                                             220);
  gtk_widget_set_vexpand(task_scroller, TRUE);

  GtkWidget *task_empty_label =
      gtk_label_new("No tasks yet. Add one to start tracking your focus.");
  gtk_widget_add_css_class(task_empty_label, "task-empty");
  gtk_widget_set_halign(task_empty_label, GTK_ALIGN_START);
  gtk_label_set_wrap(GTK_LABEL(task_empty_label), TRUE);
  state->task_empty_label = task_empty_label;

  gtk_box_append(GTK_BOX(tasks_card), tasks_title);
  gtk_box_append(GTK_BOX(tasks_card), task_input_box);
  gtk_box_append(GTK_BOX(tasks_card), task_scroller);
  gtk_box_append(GTK_BOX(tasks_card), task_empty_label);

  gtk_box_append(GTK_BOX(task_section), tasks_card);
  gtk_box_append(GTK_BOX(root), task_section);

  gtk_window_set_child(GTK_WINDOW(window), root);
  gtk_window_present(GTK_WINDOW(window));

  task_list_refresh(state);

  g_info("Main window presented");
}
