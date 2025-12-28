#include "ui/main_window.h"
#include "ui/main_window_internal.h"

#include "app/app_state.h"
#include "core/pomodoro_timer.h"
#include "core/task_store.h"
#include "focus/focus_guard.h"
#include "overlay/overlay_window.h"
#include "storage/settings_storage.h"
#include "storage/task_storage.h"
#include "tray/tray_item.h"
#include "ui/dialogs.h"
#include "ui/task_list.h"
#include "utils/autostart.h"
#include "config.h"

static gboolean
on_main_window_close_request(GtkWindow *window, gpointer user_data)
{
  AppState *state = user_data;
  if (state == NULL || window == NULL) {
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
on_main_window_surface_state_changed(GObject *object,
                                     GParamSpec *pspec,
                                     gpointer user_data)
{
  (void)pspec;
  AppState *state = user_data;
  if (state == NULL || state->window == NULL || !state->minimize_to_tray ||
      state->quit_requested) {
    return;
  }

  GdkSurface *surface = GDK_SURFACE(object);
  if (surface == NULL) {
    return;
  }

  GdkToplevelState state_flags = gdk_toplevel_get_state(GDK_TOPLEVEL(surface));
  if ((state_flags & GDK_TOPLEVEL_STATE_MINIMIZED) == 0) {
    return;
  }

  if (!gtk_widget_get_visible(GTK_WIDGET(state->window))) {
    return;
  }

  gtk_widget_set_visible(GTK_WIDGET(state->window), FALSE);
}

static void
on_main_window_realize(GtkWidget *widget, gpointer user_data)
{
  AppState *state = user_data;
  if (state == NULL) {
    return;
  }

  GtkNative *native = gtk_widget_get_native(widget);
  if (native == NULL) {
    return;
  }

  GdkSurface *surface = gtk_native_get_surface(native);
  if (surface == NULL) {
    return;
  }

  if (g_object_get_data(G_OBJECT(surface), "minimize-to-tray-connected") != NULL) {
    return;
  }

  g_signal_connect(surface,
                   "notify::state",
                   G_CALLBACK(on_main_window_surface_state_changed),
                   state);
  g_object_set_data(G_OBJECT(surface),
                    "minimize-to-tray-connected",
                    GINT_TO_POINTER(1));
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
main_window_present(GtkApplication *app, gboolean autostart_launch)
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
  state->autostart_enabled = app_settings.autostart_enabled;
  state->autostart_start_in_tray = app_settings.autostart_start_in_tray;
  state->minimize_to_tray = app_settings.minimize_to_tray;

  if (!autostart_set_enabled(state->autostart_enabled, &error)) {
    g_warning("Failed to update autostart settings: %s",
              error ? error->message : "unknown error");
    g_clear_error(&error);
  }
  pomodoro_timer_set_update_callback(timer,
                                     on_timer_tick,
                                     on_timer_phase_changed,
                                     state);

  overlay_window_create(app, state);
  FocusGuardConfig guard_config = focus_guard_config_default();
  if (!settings_storage_load_focus_guard(&guard_config, &error)) {
    g_warning("Failed to load focus guard settings: %s",
              error ? error->message : "unknown error");
    g_clear_error(&error);
  }
  state->focus_guard = focus_guard_create(state, guard_config);
  focus_guard_config_clear(&guard_config);
  tray_item_create(app, state);
  g_signal_connect(window,
                   "close-request",
                   G_CALLBACK(on_main_window_close_request),
                   state);
  g_signal_connect(window, "realize", G_CALLBACK(on_main_window_realize), state);

  GtkGesture *window_click = gtk_gesture_click_new();
  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(window_click), 0);
  gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(window_click),
                                             GTK_PHASE_CAPTURE);
  g_signal_connect(window_click,
                   "pressed",
                   G_CALLBACK(task_list_on_window_pressed),
                   state);
  gtk_widget_add_controller(window, GTK_EVENT_CONTROLLER(window_click));

  main_window_build_ui(state, autostart_launch);
}
