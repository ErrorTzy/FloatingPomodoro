#include "ui/main_window_internal.h"
#include "ui/main_window.h"

#include "core/task_store.h"
#include "overlay/overlay_window.h"
#include "tray/tray_item.h"
#include "ui/task_list.h"

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
    if (label != NULL) {
      gtk_widget_set_tooltip_text(state->timer_start_button, label);
      gtk_accessible_update_property(GTK_ACCESSIBLE(state->timer_start_button),
                                     GTK_ACCESSIBLE_PROPERTY_LABEL,
                                     label,
                                     -1);
    }
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

void
on_timer_tick(PomodoroTimer *timer, gpointer user_data)
{
  (void)timer;
  main_window_update_timer_ui((AppState *)user_data);
}

void
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
