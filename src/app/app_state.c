#include "app/app_state.h"

#include "core/pomodoro_timer.h"
#include "ui/dialogs.h"

AppState *
app_state_create(GtkWindow *window, TaskStore *store)
{
  AppState *state = g_new0(AppState, 1);
  state->window = window;
  state->store = store;
  return state;
}

void
app_state_free(gpointer data)
{
  AppState *state = data;
  if (state == NULL) {
    return;
  }

  dialogs_cleanup_settings(state);
  dialogs_cleanup_archived(state);

  if (state->overlay_window != NULL) {
    gtk_window_destroy(state->overlay_window);
    state->overlay_window = NULL;
  }

  pomodoro_timer_free(state->timer);
  task_store_free(state->store);
  g_free(state);
}
