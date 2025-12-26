#include "tray/tray_item.h"

#include "tray/tray_item_internal.h"

#include "core/task_store.h"
#include "overlay/overlay_window.h"

gboolean
tray_item_has_task(AppState *state)
{
  if (state == NULL || state->store == NULL) {
    return FALSE;
  }

  return task_store_get_active(state->store) != NULL;
}

void
tray_action_present(AppState *state, GtkApplication *app)
{
  if (state != NULL && state->window != NULL) {
    gtk_window_present(state->window);
    return;
  }

  if (app != NULL) {
    g_application_activate(G_APPLICATION(app));
  }
}

void
tray_item_create(GtkApplication *app, AppState *state)
{
  if (state == NULL || state->tray_item != NULL) {
    return;
  }

  TrayItem *tray = g_new0(TrayItem, 1);
  tray->state = state;
  tray->app = app;
  tray->menu_revision = 1;

  GError *error = NULL;
  tray->connection = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
  if (tray->connection == NULL) {
    g_warning("Failed to connect to session bus: %s",
              error ? error->message : "unknown error");
    g_clear_error(&error);
    g_free(tray);
    return;
  }

  tray->icon_pixmap = g_variant_ref_sink(tray_icon_pixmap_draw(22));

  tray_sni_register(tray);
  tray_menu_register(tray);
  tray_sni_watch(tray);

  state->tray_item = tray;
  tray_item_update(state);
}

void
tray_item_update(AppState *state)
{
  if (state == NULL || state->tray_item == NULL) {
    return;
  }

  TrayItem *tray = state->tray_item;
  if (state->timer == NULL) {
    return;
  }

  PomodoroTimerState run_state = pomodoro_timer_get_state(state->timer);
  PomodoroPhase phase = pomodoro_timer_get_phase(state->timer);
  gboolean has_task = tray_item_has_task(state);
  gboolean overlay_visible = overlay_window_is_visible(state);

  if (tray->has_state &&
      tray->last_timer_state == run_state &&
      tray->last_phase == phase &&
      tray->last_has_task == has_task &&
      tray->last_overlay_visible == overlay_visible) {
    return;
  }

  tray->has_state = TRUE;
  tray->last_timer_state = run_state;
  tray->last_phase = phase;
  tray->last_has_task = has_task;
  tray->last_overlay_visible = overlay_visible;
  tray->menu_revision++;
  tray_menu_emit_props_updated(tray);
  tray_menu_emit_layout_updated(tray);
}

void
tray_item_destroy(AppState *state)
{
  if (state == NULL || state->tray_item == NULL) {
    return;
  }

  TrayItem *tray = state->tray_item;
  state->tray_item = NULL;

  tray_sni_unwatch(tray);

  if (tray->connection != NULL) {
    tray_sni_unregister(tray);
    tray_menu_unregister(tray);
    g_object_unref(tray->connection);
  }

  if (tray->icon_pixmap != NULL) {
    g_variant_unref(tray->icon_pixmap);
  }

  g_free(tray);
}
