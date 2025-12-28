#include "ui/main_window_internal.h"

#include "core/task_store.h"
#include "overlay/overlay_window.h"
#include "ui/main_window.h"

static gboolean
main_window_has_active_task(AppState *state)
{
  if (state == NULL) {
    return FALSE;
  }

  return task_store_get_active(state->store) != NULL;
}

void
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

void
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

void
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

void
on_overlay_toggle_clicked(GtkButton *button, gpointer user_data)
{
  (void)button;
  AppState *state = user_data;
  if (state == NULL) {
    return;
  }

  overlay_window_toggle_visible(state);
}
