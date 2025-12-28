#include "app/app_state.h"

#include "core/pomodoro_timer.h"
#include "focus/focus_guard.h"
#include "tray/tray_item.h"
#include "ui/dialogs.h"

AppState *
app_state_create(GtkWindow *window, TaskStore *store)
{
  AppState *state = g_new0(AppState, 1);
  state->window = window;
  state->store = store;
  state->close_to_tray = TRUE;
  state->autostart_enabled = FALSE;
  state->autostart_start_in_tray = TRUE;
  state->minimize_to_tray = FALSE;
  state->quit_requested = FALSE;
  return state;
}

void
app_state_free(gpointer data)
{
  AppState *state = data;
  if (state == NULL) {
    return;
  }

  dialogs_cleanup_archive_settings(state);
  dialogs_cleanup_timer_settings(state);
  dialogs_cleanup_archived(state);

  tray_item_destroy(state);

  focus_guard_destroy(state->focus_guard);
  state->focus_guard = NULL;

  if (state->overlay_window != NULL) {
    gtk_window_destroy(state->overlay_window);
    state->overlay_window = NULL;
  }

  pomodoro_timer_free(state->timer);
  task_store_free(state->store);
  g_free(state);
}
