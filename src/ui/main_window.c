#include "ui/main_window.h"

#include "app/app_state.h"
#include "core/pomodoro_timer.h"
#include "core/task_store.h"
#include "overlay/overlay_window.h"
#include "storage/settings_storage.h"
#include "storage/task_storage.h"
#include "tray/tray_item.h"
#include "ui/dialogs.h"
#include "ui/task_list.h"
#include "config.h"

static const char *
timer_phase_title(PomodoroPhase phase)
{
  switch (phase) {
    case POMODORO_PHASE_SHORT_BREAK:
      return "Short Break";
    case POMODORO_PHASE_LONG_BREAK:
      return "Long Break";
    case POMODORO_PHASE_FOCUS:
    default:
      return "Focus Session";
  }
}

static const char *
timer_phase_action(PomodoroPhase phase)
{
  switch (phase) {
    case POMODORO_PHASE_SHORT_BREAK:
      return "Start Break";
    case POMODORO_PHASE_LONG_BREAK:
      return "Start Long Break";
    case POMODORO_PHASE_FOCUS:
    default:
      return "Start Focus";
  }
}

static char *
format_timer_value(gint64 seconds)
{
  if (seconds < 0) {
    seconds = 0;
  }

  gint64 minutes = seconds / 60;
  gint64 secs = seconds % 60;
  return g_strdup_printf("%02" G_GINT64_FORMAT ":%02" G_GINT64_FORMAT,
                         minutes,
                         secs);
}

static void
set_icon_button_label(GtkWidget *button, const char *label)
{
  if (button == NULL || label == NULL) {
    return;
  }

  gtk_widget_set_tooltip_text(button, label);
  gtk_accessible_update_property(GTK_ACCESSIBLE(button),
                                 GTK_ACCESSIBLE_PROPERTY_LABEL,
                                 label,
                                 -1);
}

static GtkWidget *
create_action_icon(const char *icon_name, int size)
{
  GtkWidget *icon = gtk_image_new_from_icon_name(icon_name);
  gtk_image_set_pixel_size(GTK_IMAGE(icon), size);
  return icon;
}

static void
update_timer_stats(AppState *state, PomodoroTimer *timer)
{
  if (state == NULL || timer == NULL) {
    return;
  }

  if (state->timer_focus_stat_label != NULL) {
    char *focus_text = format_timer_value(pomodoro_timer_get_focus_seconds(timer));
    gtk_label_set_text(GTK_LABEL(state->timer_focus_stat_label), focus_text);
    g_free(focus_text);
  }

  if (state->timer_break_stat_label != NULL) {
    char *breaks =
        g_strdup_printf("%u", pomodoro_timer_get_breaks_completed(timer));
    gtk_label_set_text(GTK_LABEL(state->timer_break_stat_label), breaks);
    g_free(breaks);
  }
}

static gboolean
main_window_has_active_task(AppState *state)
{
  if (state == NULL) {
    return FALSE;
  }

  return task_store_get_active(state->store) != NULL;
}

void
main_window_update_timer_ui(AppState *state)
{
  if (state == NULL || state->timer == NULL) {
    return;
  }

  PomodoroTimer *timer = state->timer;
  PomodoroTimerState run_state = pomodoro_timer_get_state(timer);
  gboolean has_task = main_window_has_active_task(state);

  if (!has_task && run_state != POMODORO_TIMER_STOPPED) {
    pomodoro_timer_stop(timer);
    return;
  }

  PomodoroPhase phase = pomodoro_timer_get_phase(timer);
  PomodoroPhase next_phase = pomodoro_timer_get_next_phase(timer);

  if (state->timer_title_label != NULL) {
    gtk_label_set_text(GTK_LABEL(state->timer_title_label),
                       timer_phase_title(phase));
  }

  if (state->timer_value_label != NULL) {
    char *value = format_timer_value(pomodoro_timer_get_remaining_seconds(timer));
    gtk_label_set_text(GTK_LABEL(state->timer_value_label), value);
    g_free(value);
  }

  if (state->timer_pill_label != NULL) {
    const char *next_label = timer_phase_title(next_phase);
    char *pill_text = g_strdup_printf("Next: %s", next_label);
    gtk_label_set_text(GTK_LABEL(state->timer_pill_label), pill_text);
    g_free(pill_text);
  }

  if (state->timer_start_button != NULL) {
    const char *label = NULL;
    const char *icon_name = "media-playback-start-symbolic";
    if (run_state == POMODORO_TIMER_RUNNING) {
      label = "Pause";
      icon_name = "media-playback-pause-symbolic";
    } else if (run_state == POMODORO_TIMER_PAUSED) {
      label = "Resume";
    } else {
      label = timer_phase_action(phase);
    }
    set_icon_button_label(state->timer_start_button, label);
    if (state->timer_start_icon != NULL) {
      gtk_image_set_from_icon_name(GTK_IMAGE(state->timer_start_icon), icon_name);
    }
    gtk_widget_set_sensitive(state->timer_start_button, has_task);
  }

  if (state->timer_skip_button != NULL) {
    gtk_widget_set_sensitive(state->timer_skip_button,
                             has_task && run_state != POMODORO_TIMER_STOPPED);
  }

  if (state->timer_stop_button != NULL) {
    gtk_widget_set_sensitive(state->timer_stop_button,
                             has_task && run_state != POMODORO_TIMER_STOPPED);
  }

  update_timer_stats(state, timer);
  overlay_window_update(state);
  tray_item_update(state);
}

static void
on_timer_tick(PomodoroTimer *timer, gpointer user_data)
{
  (void)timer;
  main_window_update_timer_ui((AppState *)user_data);
}

static void
on_timer_phase_changed(PomodoroTimer *timer, gpointer user_data)
{
  AppState *state = user_data;
  if (state == NULL || timer == NULL) {
    return;
  }

  PomodoroPhase phase = pomodoro_timer_get_phase(timer);
  if (phase == POMODORO_PHASE_SHORT_BREAK ||
      phase == POMODORO_PHASE_LONG_BREAK) {
    PomodoroTask *task = task_store_get_active(state->store);
    if (task != NULL) {
      guint repeats = pomodoro_task_get_repeat_count(task);
      if (repeats <= 1) {
        task_store_complete(state->store, task);
      } else {
        pomodoro_task_set_repeat_count(task, repeats - 1);
      }
      task_store_apply_archive_policy(state->store);
      task_list_save_store(state);
      task_list_refresh(state);
    }
  }

  main_window_update_timer_ui(state);
}

static void
on_timer_start_clicked(GtkButton *button, gpointer user_data)
{
  (void)button;
  AppState *state = user_data;
  if (state == NULL || state->timer == NULL) {
    return;
  }

  if (!main_window_has_active_task(state)) {
    return;
  }

  pomodoro_timer_toggle(state->timer);
  main_window_update_timer_ui(state);
}

static void
on_timer_skip_clicked(GtkButton *button, gpointer user_data)
{
  (void)button;
  AppState *state = user_data;
  if (state == NULL || state->timer == NULL) {
    return;
  }

  pomodoro_timer_skip(state->timer);
  main_window_update_timer_ui(state);
}

static void
on_timer_stop_clicked(GtkButton *button, gpointer user_data)
{
  (void)button;
  AppState *state = user_data;
  if (state == NULL || state->timer == NULL) {
    return;
  }

  pomodoro_timer_stop(state->timer);
  main_window_update_timer_ui(state);
}

static void
on_overlay_toggle_clicked(GtkButton *button, gpointer user_data)
{
  (void)button;
  AppState *state = user_data;
  if (state == NULL) {
    return;
  }

  overlay_window_toggle_visible(state);
}

static gboolean
on_main_window_close_request(GtkWindow *window, gpointer user_data)
{
  AppState *state = user_data;
  if (state == NULL) {
    return FALSE;
  }

  if (state->quit_requested) {
    return FALSE;
  }

  if (state->close_to_tray) {
    gtk_widget_set_visible(GTK_WIDGET(window), FALSE);
    return TRUE;
  }

  GtkApplication *app = gtk_window_get_application(window);
  if (app != NULL) {
    state->quit_requested = TRUE;
    g_application_quit(G_APPLICATION(app));
    return TRUE;
  }

  return FALSE;
}

static void
on_main_window_destroy(GtkWidget *widget, gpointer user_data)
{
  GtkApplication *app = user_data;
  if (app == NULL) {
    return;
  }

  if (g_object_get_data(G_OBJECT(app), "main-window") == widget) {
    g_object_set_data(G_OBJECT(app), "main-window", NULL);
  }
}

void
main_window_present(GtkApplication *app)
{
  if (app != NULL) {
    GtkWindow *existing = g_object_get_data(G_OBJECT(app), "main-window");
    if (existing != NULL) {
      gtk_window_present(existing);
      return;
    }
  }

  GtkWidget *window = gtk_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(window), APP_NAME);
  gtk_window_set_default_size(GTK_WINDOW(window), 880, 560);
  gtk_widget_add_css_class(window, "app-window");
  if (app != NULL) {
    g_object_set_data(G_OBJECT(app), "main-window", window);
    g_signal_connect(window,
                     "destroy",
                     G_CALLBACK(on_main_window_destroy),
                     app);
  }

  TaskStore *store = task_store_new();
  GError *error = NULL;
  if (!task_storage_load(store, &error)) {
    g_warning("Failed to load tasks: %s", error ? error->message : "unknown error");
    g_clear_error(&error);
  }
  task_store_apply_archive_policy(store);

  PomodoroTimerConfig timer_config = pomodoro_timer_config_default();
  if (!settings_storage_load_timer(&timer_config, &error)) {
    g_warning("Failed to load timer settings: %s",
              error ? error->message : "unknown error");
    g_clear_error(&error);
  }
  PomodoroTimer *timer = pomodoro_timer_new(timer_config);
  const char *test_timing = g_getenv("POMODORO_TEST_TIMER");
  if (test_timing != NULL &&
      (g_ascii_strcasecmp(test_timing, "1") == 0 ||
       g_ascii_strcasecmp(test_timing, "true") == 0 ||
       g_ascii_strcasecmp(test_timing, "yes") == 0)) {
    pomodoro_timer_set_test_durations(timer, 2500, 2000, 2000, 500);
  }

  AppState *state = app_state_create(GTK_WINDOW(window), store);
  g_object_set_data_full(G_OBJECT(window), "app-state", state, app_state_free);
  state->timer = timer;
  AppSettings app_settings = {0};
  if (!settings_storage_load_app(&app_settings, &error)) {
    g_warning("Failed to load app settings: %s",
              error ? error->message : "unknown error");
    g_clear_error(&error);
  }
  state->close_to_tray = app_settings.close_to_tray;
  pomodoro_timer_set_update_callback(timer,
                                     on_timer_tick,
                                     on_timer_phase_changed,
                                     state);

  overlay_window_create(app, state);
  tray_item_create(app, state);
  g_signal_connect(window,
                   "close-request",
                   G_CALLBACK(on_main_window_close_request),
                   state);

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

  GtkWidget *subtitle = gtk_label_new("Start a focus session when you're ready.");
  gtk_widget_add_css_class(subtitle, "app-subtitle");
  gtk_widget_set_halign(subtitle, GTK_ALIGN_START);
  gtk_label_set_wrap(GTK_LABEL(subtitle), TRUE);

  gtk_box_append(GTK_BOX(header), title);
  gtk_box_append(GTK_BOX(header), subtitle);
  gtk_box_append(GTK_BOX(root), header);

  GtkWidget *action_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  gtk_widget_set_halign(action_row, GTK_ALIGN_START);

  GtkWidget *timer_settings_button = gtk_button_new();
  gtk_widget_add_css_class(timer_settings_button, "icon-button");
  gtk_widget_set_size_request(timer_settings_button, 36, 36);
  gtk_widget_set_valign(timer_settings_button, GTK_ALIGN_CENTER);
  GtkWidget *timer_settings_icon =
      create_action_icon("pomodoro-timer-settings-symbolic", 20);
  gtk_button_set_child(GTK_BUTTON(timer_settings_button), timer_settings_icon);
  set_icon_button_label(timer_settings_button, "Timer settings");
  g_signal_connect(timer_settings_button,
                   "clicked",
                   G_CALLBACK(dialogs_on_show_timer_settings_clicked),
                   state);

  GtkWidget *archived_button = gtk_button_new();
  gtk_widget_add_css_class(archived_button, "icon-button");
  gtk_widget_set_size_request(archived_button, 36, 36);
  gtk_widget_set_valign(archived_button, GTK_ALIGN_CENTER);
  GtkWidget *archived_icon =
      create_action_icon("pomodoro-archive-symbolic", 20);
  gtk_button_set_child(GTK_BUTTON(archived_button), archived_icon);
  set_icon_button_label(archived_button, "Archived tasks");
  g_signal_connect(archived_button,
                   "clicked",
                   G_CALLBACK(dialogs_on_show_archived_clicked),
                   state);

  GtkWidget *overlay_toggle_button = gtk_button_new();
  gtk_widget_add_css_class(overlay_toggle_button, "icon-button");
  gtk_widget_set_size_request(overlay_toggle_button, 36, 36);
  gtk_widget_set_valign(overlay_toggle_button, GTK_ALIGN_CENTER);
  g_signal_connect(overlay_toggle_button,
                   "clicked",
                   G_CALLBACK(on_overlay_toggle_clicked),
                   state);
  state->overlay_toggle_button = overlay_toggle_button;

  GtkWidget *overlay_toggle_icon =
      gtk_image_new_from_icon_name("pomodoro-overlay-hide-symbolic");
  gtk_image_set_pixel_size(GTK_IMAGE(overlay_toggle_icon), 22);
  gtk_button_set_child(GTK_BUTTON(overlay_toggle_button), overlay_toggle_icon);
  state->overlay_toggle_icon = overlay_toggle_icon;
  overlay_window_sync_toggle_icon(state);

  gtk_box_append(GTK_BOX(action_row), timer_settings_button);
  gtk_box_append(GTK_BOX(action_row), archived_button);
  gtk_box_append(GTK_BOX(action_row), overlay_toggle_button);
  gtk_box_append(GTK_BOX(root), action_row);

  GtkWidget *hero = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 18);
  gtk_widget_set_hexpand(hero, TRUE);

  GtkWidget *timer_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_add_css_class(timer_card, "card");
  gtk_widget_set_hexpand(timer_card, TRUE);

  GtkWidget *timer_title = gtk_label_new("Focus Session");
  gtk_widget_add_css_class(timer_title, "card-title");
  gtk_widget_set_halign(timer_title, GTK_ALIGN_START);
  state->timer_title_label = timer_title;

  GtkWidget *timer_value = gtk_label_new("25:00");
  gtk_widget_add_css_class(timer_value, "timer-value");
  gtk_widget_set_halign(timer_value, GTK_ALIGN_START);
  state->timer_value_label = timer_value;

  GtkWidget *timer_pill = gtk_label_new("Next: short break");
  gtk_widget_add_css_class(timer_pill, "pill");
  gtk_widget_set_halign(timer_pill, GTK_ALIGN_START);
  state->timer_pill_label = timer_pill;

  GtkWidget *timer_actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  gtk_widget_set_halign(timer_actions, GTK_ALIGN_START);

  GtkWidget *start_button = gtk_button_new();
  gtk_widget_add_css_class(start_button, "icon-button");
  gtk_widget_set_size_request(start_button, 40, 40);
  GtkWidget *start_icon =
      create_action_icon("media-playback-start-symbolic", 22);
  gtk_button_set_child(GTK_BUTTON(start_button), start_icon);
  set_icon_button_label(start_button, "Start Focus");
  g_signal_connect(start_button,
                   "clicked",
                   G_CALLBACK(on_timer_start_clicked),
                   state);
  state->timer_start_button = start_button;
  state->timer_start_icon = start_icon;

  GtkWidget *skip_button = gtk_button_new();
  gtk_widget_add_css_class(skip_button, "icon-button");
  gtk_widget_set_size_request(skip_button, 40, 40);
  GtkWidget *skip_icon =
      create_action_icon("media-skip-forward-symbolic", 20);
  gtk_button_set_child(GTK_BUTTON(skip_button), skip_icon);
  set_icon_button_label(skip_button, "Skip");
  g_signal_connect(skip_button,
                   "clicked",
                   G_CALLBACK(on_timer_skip_clicked),
                   state);
  state->timer_skip_button = skip_button;

  GtkWidget *stop_button = gtk_button_new();
  gtk_widget_add_css_class(stop_button, "icon-button");
  gtk_widget_add_css_class(stop_button, "icon-danger");
  gtk_widget_set_size_request(stop_button, 40, 40);
  GtkWidget *stop_icon =
      create_action_icon("media-playback-stop-symbolic", 20);
  gtk_button_set_child(GTK_BUTTON(stop_button), stop_icon);
  set_icon_button_label(stop_button, "Stop");
  g_signal_connect(stop_button,
                   "clicked",
                   G_CALLBACK(on_timer_stop_clicked),
                   state);
  state->timer_stop_button = stop_button;

  gtk_box_append(GTK_BOX(timer_actions), start_button);
  gtk_box_append(GTK_BOX(timer_actions), skip_button);
  gtk_box_append(GTK_BOX(timer_actions), stop_button);

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

  GtkWidget *task_meta = gtk_label_new("Add a task below or activate a pending one");
  gtk_widget_add_css_class(task_meta, "task-meta");
  gtk_widget_set_halign(task_meta, GTK_ALIGN_START);
  state->current_task_meta = task_meta;

  GtkWidget *stats_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 18);
  gtk_widget_set_halign(stats_row, GTK_ALIGN_START);

  GtkWidget *stat_block_left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  GtkWidget *stat_value_left = gtk_label_new("00:00");
  gtk_widget_add_css_class(stat_value_left, "stat-value");
  state->timer_focus_stat_label = stat_value_left;
  GtkWidget *stat_label_left = gtk_label_new("Focus time");
  gtk_widget_add_css_class(stat_label_left, "stat-label");
  gtk_box_append(GTK_BOX(stat_block_left), stat_value_left);
  gtk_box_append(GTK_BOX(stat_block_left), stat_label_left);

  GtkWidget *stat_block_right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  GtkWidget *stat_value_right = gtk_label_new("0");
  gtk_widget_add_css_class(stat_value_right, "stat-value");
  state->timer_break_stat_label = stat_value_right;
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

  GtkWidget *task_add_button = gtk_button_new();
  gtk_widget_add_css_class(task_add_button, "icon-button");
  gtk_widget_add_css_class(task_add_button, "task-add");
  gtk_widget_set_size_request(task_add_button, 36, 36);
  gtk_widget_set_valign(task_add_button, GTK_ALIGN_CENTER);
  GtkWidget *task_add_icon =
      create_action_icon("list-add-symbolic", 20);
  gtk_button_set_child(GTK_BUTTON(task_add_button), task_add_icon);
  set_icon_button_label(task_add_button, "Add task");
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
  main_window_update_timer_ui(state);

  g_info("Main window presented");
}
