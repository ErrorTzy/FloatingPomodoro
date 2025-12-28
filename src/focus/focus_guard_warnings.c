#include "focus/focus_guard_internal.h"

#include "core/pomodoro_timer.h"
#include "core/task_store.h"
#include "focus/focus_guard_x11.h"
#include "overlay/overlay_window.h"

void
focus_guard_set_warning(FocusGuard *guard, gboolean active, const char *text)
{
  if (guard == NULL || guard->state == NULL) {
    return;
  }

  if (!active) {
    if (!guard->warning_active) {
      return;
    }
    guard->warning_active = FALSE;
    g_clear_pointer(&guard->warning_app, g_free);
    overlay_window_set_warning(guard->state, FALSE, NULL);
    return;
  }

  if (!guard->warning_active ||
      g_strcmp0(guard->warning_app, text) != 0) {
    g_free(guard->warning_app);
    guard->warning_app = g_strdup(text != NULL ? text : "");
  }

  guard->warning_active = TRUE;

  if (!overlay_window_is_visible(guard->state)) {
    overlay_window_set_visible(guard->state, TRUE);
  }

  overlay_window_set_warning(guard->state, TRUE, guard->warning_app);
}

gboolean
focus_guard_should_track(const FocusGuard *guard)
{
  if (guard == NULL || guard->state == NULL || guard->state->timer == NULL ||
      guard->state->store == NULL) {
    return FALSE;
  }

  PomodoroTimerState run_state = pomodoro_timer_get_state(guard->state->timer);
  if (run_state != POMODORO_TIMER_RUNNING) {
    return FALSE;
  }

  PomodoroPhase phase = pomodoro_timer_get_phase(guard->state->timer);
  if (phase != POMODORO_PHASE_FOCUS) {
    return FALSE;
  }

  return task_store_get_active(guard->state->store) != NULL;
}

void
focus_guard_build_blacklist(FocusGuard *guard)
{
  if (guard == NULL) {
    return;
  }

  g_strfreev(guard->blacklist_norm);
  guard->blacklist_norm = NULL;

  if (guard->config.blacklist == NULL) {
    guard->blacklist_norm = g_new0(char *, 1);
    return;
  }

  gsize count = g_strv_length(guard->config.blacklist);
  guard->blacklist_norm = g_new0(char *, count + 1);

  for (gsize i = 0; i < count; i++) {
    const char *value = guard->config.blacklist[i];
    if (value == NULL) {
      continue;
    }
    guard->blacklist_norm[i] = g_ascii_strdown(value, -1);
  }
}

gboolean
focus_guard_is_blacklisted(FocusGuard *guard, const char *app_key)
{
  if (guard == NULL || app_key == NULL || guard->blacklist_norm == NULL) {
    return FALSE;
  }

  for (gsize i = 0; guard->blacklist_norm[i] != NULL; i++) {
    const char *value = guard->blacklist_norm[i];
    if (value == NULL || *value == '\0') {
      continue;
    }
    if (g_strstr_len(app_key, -1, value) != NULL) {
      return TRUE;
    }
  }

  return FALSE;
}

gboolean
focus_guard_is_chrome_app(const char *app_key)
{
  if (app_key == NULL || *app_key == '\0') {
    return FALSE;
  }

  return g_strstr_len(app_key, -1, "chrome") != NULL ||
         g_strstr_len(app_key, -1, "chromium") != NULL;
}

void
focus_guard_refresh_warning(FocusGuard *guard,
                            const char *app_name,
                            const char *app_key)
{
  if (guard == NULL) {
    return;
  }

  if (!focus_guard_should_track(guard) || !guard->config.warnings_enabled) {
    focus_guard_set_warning(guard, FALSE, NULL);
    return;
  }

  if (app_key != NULL && focus_guard_is_blacklisted(guard, app_key)) {
    focus_guard_set_warning(guard, TRUE, app_name != NULL ? app_name : app_key);
    return;
  }

  if (guard->relevance_warning_active &&
      app_key != NULL &&
      focus_guard_is_chrome_app(app_key)) {
    focus_guard_set_warning(guard,
                            TRUE,
                            guard->relevance_warning_text != NULL
                                ? guard->relevance_warning_text
                                : "Chrome");
    return;
  }

  focus_guard_set_warning(guard, FALSE, NULL);
}

void
focus_guard_refresh_warning_from_active(FocusGuard *guard)
{
  if (guard == NULL) {
    return;
  }

  char *app_name = NULL;
  char *app_key = NULL;
  if (focus_guard_x11_get_active_app(&app_name, NULL) && app_name != NULL) {
    app_key = g_ascii_strdown(app_name, -1);
  }

  focus_guard_refresh_warning(guard, app_name, app_key);

  g_free(app_key);
  g_free(app_name);
}
