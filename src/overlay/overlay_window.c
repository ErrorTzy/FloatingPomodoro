#include "overlay/overlay_window.h"

#include "overlay/overlay_window_internal.h"

#include "core/task_store.h"
#include "tray/tray_item.h"
#include "utils/x11.h"

static OverlayWindow *
overlay_from_state(AppState *state)
{
  if (state == NULL || state->overlay_window == NULL) {
    return NULL;
  }

  return g_object_get_data(G_OBJECT(state->overlay_window), "overlay-window");
}

static void
overlay_window_free(gpointer data)
{
  OverlayWindow *overlay = data;
  if (overlay == NULL) {
    return;
  }

  g_free(overlay);
}

gboolean
overlay_window_is_visible(AppState *state)
{
  if (state == NULL || state->overlay_window == NULL) {
    return FALSE;
  }

  return gtk_widget_get_visible(GTK_WIDGET(state->overlay_window));
}

static void
overlay_window_update_toggle_icon(AppState *state)
{
  if (state == NULL || state->overlay_toggle_icon == NULL) {
    return;
  }

  gboolean visible = overlay_window_is_visible(state);
  const char *icon_name = visible ? "pomodoro-overlay-hide-symbolic"
                                  : "pomodoro-overlay-show-symbolic";
  gtk_image_set_from_icon_name(GTK_IMAGE(state->overlay_toggle_icon), icon_name);

  if (state->overlay_toggle_button != NULL) {
    const char *label = visible ? "Hide floating ball" : "Show floating ball";
    gtk_widget_set_tooltip_text(state->overlay_toggle_button, label);
    gtk_accessible_update_property(GTK_ACCESSIBLE(state->overlay_toggle_button),
                                   GTK_ACCESSIBLE_PROPERTY_LABEL,
                                   label,
                                   -1);
  }
}

void
overlay_window_sync_toggle_icon(AppState *state)
{
  overlay_window_update_toggle_icon(state);
  tray_item_update(state);
}

static char *
overlay_window_format_timer_value(gint64 seconds)
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

static const char *
overlay_window_phase_title(PomodoroPhase phase)
{
  switch (phase) {
    case POMODORO_PHASE_SHORT_BREAK:
      return "Short Break";
    case POMODORO_PHASE_LONG_BREAK:
      return "Long Break";
    case POMODORO_PHASE_FOCUS:
    default:
      return "Focus";
  }
}

static const char *
overlay_window_phase_action(PomodoroPhase phase)
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

static const char *
overlay_window_phase_label_for_state(PomodoroTimerState state,
                                    PomodoroPhase phase)
{
  if (state == POMODORO_TIMER_PAUSED) {
    return "Paused";
  }

  if (state == POMODORO_TIMER_STOPPED) {
    return "Ready";
  }

  return overlay_window_phase_title(phase);
}

static PomodoroTask *
overlay_window_find_next_task(TaskStore *store, PomodoroTask *active)
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
    if (task == NULL || task == active) {
      continue;
    }

    TaskStatus status = pomodoro_task_get_status(task);
    if (status == TASK_STATUS_PENDING) {
      return task;
    }
  }

  return NULL;
}

static gboolean
overlay_window_apply_x11_hints_idle(gpointer data)
{
  OverlayWindow *overlay = data;
  if (overlay == NULL || overlay->window == NULL) {
    return G_SOURCE_REMOVE;
  }

  x11_window_set_keep_above(overlay->window, TRUE);
  x11_window_set_skip_taskbar(overlay->window, TRUE);
  x11_window_set_skip_pager(overlay->window, TRUE);
  return G_SOURCE_REMOVE;
}

void
overlay_window_create(GtkApplication *app, AppState *state)
{
  if (app == NULL || state == NULL) {
    return;
  }

  if (state->overlay_window != NULL) {
    return;
  }

  GtkWindow *window = overlay_window_create_window(app);

  OverlayWindow *overlay = g_new0(OverlayWindow, 1);
  overlay->state = state;
  overlay->window = window;
  overlay->opacity = 0.65;
  overlay->phase = POMODORO_PHASE_FOCUS;
  overlay->timer_state = POMODORO_TIMER_STOPPED;
  overlay->progress = 0.0;

  g_object_set_data_full(G_OBJECT(window),
                         "overlay-window",
                         overlay,
                         overlay_window_free);

  state->overlay_window = window;
  g_object_add_weak_pointer(G_OBJECT(window),
                            (gpointer *)&state->overlay_window);

  overlay_window_build_ui(overlay);

  gtk_window_present(GTK_WINDOW(window));
  g_idle_add(overlay_window_apply_x11_hints_idle, overlay);

  overlay_window_update(state);
}

void
overlay_window_update(AppState *state)
{
  OverlayWindow *overlay = overlay_from_state(state);
  if (overlay == NULL || state == NULL || state->timer == NULL) {
    return;
  }

  PomodoroTimer *timer = state->timer;
  overlay->timer_state = pomodoro_timer_get_state(timer);
  overlay->phase = pomodoro_timer_get_phase(timer);

  gint64 remaining_seconds = pomodoro_timer_get_remaining_seconds(timer);
  gint64 total_seconds =
      pomodoro_timer_get_phase_total_seconds(timer, overlay->phase);
  if (total_seconds < 1) {
    total_seconds = 1;
  }

  gdouble progress = 1.0 - ((gdouble)remaining_seconds / (gdouble)total_seconds);
  if (overlay->timer_state == POMODORO_TIMER_STOPPED) {
    progress = 0.0;
  }

  if (progress < 0.0) {
    progress = 0.0;
  } else if (progress > 1.0) {
    progress = 1.0;
  }

  overlay->progress = progress;

  if (overlay->time_label != NULL) {
    char *time_text = overlay_window_format_timer_value(remaining_seconds);
    gtk_label_set_text(GTK_LABEL(overlay->time_label), time_text);
    g_free(time_text);
  }

  if (overlay->phase_label != NULL) {
    gtk_label_set_text(GTK_LABEL(overlay->phase_label),
                       overlay_window_phase_label_for_state(overlay->timer_state,
                                                            overlay->phase));
  }

  PomodoroTask *active_task = task_store_get_active(state->store);
  PomodoroTask *next_task =
      overlay_window_find_next_task(state->store, active_task);

  if (overlay->current_task_label != NULL) {
    const char *title = active_task ? pomodoro_task_get_title(active_task)
                                    : "No active task";
    gtk_label_set_text(GTK_LABEL(overlay->current_task_label), title);
    gtk_widget_set_tooltip_text(overlay->current_task_label, title);
  }

  if (overlay->next_task_label != NULL) {
    const char *next_title = next_task ? pomodoro_task_get_title(next_task)
                                       : "Pick one from the list";
    gtk_label_set_text(GTK_LABEL(overlay->next_task_label), next_title);
    gtk_widget_set_tooltip_text(overlay->next_task_label, next_title);
  }

  if (overlay->menu_toggle_button != NULL) {
    const char *label = NULL;
    const char *icon_name = "media-playback-start-symbolic";
    if (overlay->timer_state == POMODORO_TIMER_RUNNING) {
      label = "Pause";
      icon_name = "media-playback-pause-symbolic";
    } else if (overlay->timer_state == POMODORO_TIMER_PAUSED) {
      label = "Resume";
    } else {
      label = overlay_window_phase_action(overlay->phase);
    }
    gtk_widget_set_tooltip_text(overlay->menu_toggle_button, label);
    gtk_accessible_update_property(GTK_ACCESSIBLE(overlay->menu_toggle_button),
                                   GTK_ACCESSIBLE_PROPERTY_LABEL,
                                   label,
                                   -1);
    if (overlay->menu_toggle_icon != NULL) {
      gtk_image_set_from_icon_name(GTK_IMAGE(overlay->menu_toggle_icon),
                                   icon_name);
    }
  }

  gboolean has_task = active_task != NULL;
  if (overlay->menu_skip_button != NULL) {
    gtk_widget_set_sensitive(overlay->menu_skip_button,
                             has_task &&
                                 overlay->timer_state != POMODORO_TIMER_STOPPED);
  }

  if (overlay->menu_stop_button != NULL) {
    gtk_widget_set_sensitive(overlay->menu_stop_button,
                             has_task &&
                                 overlay->timer_state != POMODORO_TIMER_STOPPED);
  }

  overlay_window_set_phase_class(overlay);

  if (overlay->drawing_area != NULL) {
    gtk_widget_queue_draw(overlay->drawing_area);
  }
}

void
overlay_window_set_visible(AppState *state, gboolean visible)
{
  if (state == NULL || state->overlay_window == NULL) {
    return;
  }

  gboolean is_visible = overlay_window_is_visible(state);
  if (visible == is_visible) {
    overlay_window_update_toggle_icon(state);
    return;
  }

  OverlayWindow *overlay = overlay_from_state(state);
  if (visible) {
    gtk_widget_set_visible(GTK_WIDGET(state->overlay_window), TRUE);
    gtk_window_present(state->overlay_window);
    if (overlay != NULL) {
      g_idle_add(overlay_window_apply_x11_hints_idle, overlay);
      overlay_window_sync_hover_state(overlay);
    }
  } else {
    if (overlay != NULL) {
      overlay_window_set_info_revealed(overlay, FALSE, FALSE);
      if (overlay->menu_open) {
        overlay_window_menu_popdown(overlay);
      }
    }
    gtk_widget_set_visible(GTK_WIDGET(state->overlay_window), FALSE);
  }

  overlay_window_update_toggle_icon(state);
}

void
overlay_window_toggle_visible(AppState *state)
{
  if (state == NULL) {
    return;
  }

  overlay_window_set_visible(state, !overlay_window_is_visible(state));
}
