#include "core/pomodoro_timer.h"

struct _PomodoroTimer {
  PomodoroTimerConfig config;
  PomodoroPhase phase;
  PomodoroTimerState state;
  gint64 remaining_seconds;
  guint focus_sessions_completed;
  guint breaks_completed;
  gint64 focus_seconds_total;
  gint64 break_seconds_total;
  guint tick_source_id;
  PomodoroTimerUpdateFn tick_cb;
  PomodoroTimerUpdateFn phase_cb;
  gpointer user_data;
};

static gint64
pomodoro_timer_phase_seconds(const PomodoroTimer *timer, PomodoroPhase phase)
{
  if (timer == NULL) {
    return 0;
  }

  switch (phase) {
    case POMODORO_PHASE_SHORT_BREAK:
      return (gint64)timer->config.short_break_minutes * 60;
    case POMODORO_PHASE_LONG_BREAK:
      return (gint64)timer->config.long_break_minutes * 60;
    case POMODORO_PHASE_FOCUS:
    default:
      return (gint64)timer->config.focus_minutes * 60;
  }
}

PomodoroTimerConfig
pomodoro_timer_config_default(void)
{
  PomodoroTimerConfig config = {0};
  config.focus_minutes = 25;
  config.short_break_minutes = 5;
  config.long_break_minutes = 15;
  config.long_break_interval = 4;
  return config;
}

PomodoroTimerConfig
pomodoro_timer_config_normalize(PomodoroTimerConfig config)
{
  if (config.focus_minutes < 1) {
    config.focus_minutes = 25;
  }
  if (config.short_break_minutes < 1) {
    config.short_break_minutes = 5;
  }
  if (config.long_break_minutes < 1) {
    config.long_break_minutes = 15;
  }
  if (config.long_break_interval < 1) {
    config.long_break_interval = 4;
  }
  return config;
}

static void
pomodoro_timer_stop_tick(PomodoroTimer *timer)
{
  if (timer == NULL) {
    return;
  }

  if (timer->tick_source_id != 0) {
    g_source_remove(timer->tick_source_id);
    timer->tick_source_id = 0;
  }
}

static void
pomodoro_timer_fire_tick(PomodoroTimer *timer)
{
  if (timer != NULL && timer->tick_cb != NULL) {
    timer->tick_cb(timer, timer->user_data);
  }
}

static void
pomodoro_timer_fire_phase(PomodoroTimer *timer)
{
  if (timer != NULL && timer->phase_cb != NULL) {
    timer->phase_cb(timer, timer->user_data);
  }
}

static PomodoroPhase
pomodoro_timer_break_for_count(const PomodoroTimer *timer, guint focus_count)
{
  if (timer == NULL) {
    return POMODORO_PHASE_SHORT_BREAK;
  }

  guint interval = timer->config.long_break_interval;
  if (interval < 1) {
    interval = 1;
  }

  if (focus_count < 1) {
    focus_count = 1;
  }

  if (focus_count % interval == 0) {
    return POMODORO_PHASE_LONG_BREAK;
  }

  return POMODORO_PHASE_SHORT_BREAK;
}

static void
pomodoro_timer_advance_phase(PomodoroTimer *timer)
{
  if (timer == NULL) {
    return;
  }

  if (timer->phase == POMODORO_PHASE_FOCUS) {
    timer->focus_sessions_completed += 1;
    timer->phase =
        pomodoro_timer_break_for_count(timer, timer->focus_sessions_completed);
  } else {
    timer->breaks_completed += 1;
    timer->phase = POMODORO_PHASE_FOCUS;
  }

  timer->remaining_seconds = pomodoro_timer_phase_seconds(timer, timer->phase);
}

static gboolean
pomodoro_timer_on_tick(gpointer data)
{
  PomodoroTimer *timer = data;
  if (timer == NULL) {
    return G_SOURCE_REMOVE;
  }

  if (timer->state != POMODORO_TIMER_RUNNING) {
    return G_SOURCE_REMOVE;
  }

  if (timer->remaining_seconds > 0) {
    timer->remaining_seconds -= 1;
  }

  if (timer->phase == POMODORO_PHASE_FOCUS) {
    timer->focus_seconds_total += 1;
  } else {
    timer->break_seconds_total += 1;
  }

  if (timer->remaining_seconds <= 0) {
    pomodoro_timer_advance_phase(timer);
    pomodoro_timer_fire_phase(timer);
  }

  pomodoro_timer_fire_tick(timer);
  return G_SOURCE_CONTINUE;
}

PomodoroTimer *
pomodoro_timer_new(PomodoroTimerConfig config)
{
  PomodoroTimer *timer = g_new0(PomodoroTimer, 1);
  timer->config = pomodoro_timer_config_normalize(config);
  timer->phase = POMODORO_PHASE_FOCUS;
  timer->state = POMODORO_TIMER_STOPPED;
  timer->remaining_seconds = pomodoro_timer_phase_seconds(timer, timer->phase);
  return timer;
}

void
pomodoro_timer_free(PomodoroTimer *timer)
{
  if (timer == NULL) {
    return;
  }

  pomodoro_timer_stop_tick(timer);
  g_free(timer);
}

void
pomodoro_timer_set_update_callback(PomodoroTimer *timer,
                                   PomodoroTimerUpdateFn tick_cb,
                                   PomodoroTimerUpdateFn phase_cb,
                                   gpointer user_data)
{
  if (timer == NULL) {
    return;
  }

  timer->tick_cb = tick_cb;
  timer->phase_cb = phase_cb;
  timer->user_data = user_data;
}

void
pomodoro_timer_apply_config(PomodoroTimer *timer, PomodoroTimerConfig config)
{
  if (timer == NULL) {
    return;
  }

  timer->config = pomodoro_timer_config_normalize(config);

  gint64 phase_seconds = pomodoro_timer_phase_seconds(timer, timer->phase);
  if (timer->state != POMODORO_TIMER_RUNNING) {
    timer->remaining_seconds = phase_seconds;
  } else if (timer->remaining_seconds > phase_seconds) {
    timer->remaining_seconds = phase_seconds;
  }

  pomodoro_timer_fire_tick(timer);
}

PomodoroTimerConfig
pomodoro_timer_get_config(const PomodoroTimer *timer)
{
  if (timer == NULL) {
    return pomodoro_timer_config_default();
  }
  return timer->config;
}

PomodoroPhase
pomodoro_timer_get_phase(const PomodoroTimer *timer)
{
  return timer ? timer->phase : POMODORO_PHASE_FOCUS;
}

PomodoroPhase
pomodoro_timer_get_next_phase(const PomodoroTimer *timer)
{
  if (timer == NULL) {
    return POMODORO_PHASE_SHORT_BREAK;
  }

  if (timer->phase == POMODORO_PHASE_FOCUS) {
    guint upcoming = timer->focus_sessions_completed + 1;
    return pomodoro_timer_break_for_count(timer, upcoming);
  }

  return POMODORO_PHASE_FOCUS;
}

PomodoroTimerState
pomodoro_timer_get_state(const PomodoroTimer *timer)
{
  return timer ? timer->state : POMODORO_TIMER_STOPPED;
}

gint64
pomodoro_timer_get_remaining_seconds(const PomodoroTimer *timer)
{
  return timer ? timer->remaining_seconds : 0;
}

gint64
pomodoro_timer_get_focus_seconds(const PomodoroTimer *timer)
{
  return timer ? timer->focus_seconds_total : 0;
}

gint64
pomodoro_timer_get_break_seconds(const PomodoroTimer *timer)
{
  return timer ? timer->break_seconds_total : 0;
}

guint
pomodoro_timer_get_focus_sessions_completed(const PomodoroTimer *timer)
{
  return timer ? timer->focus_sessions_completed : 0;
}

guint
pomodoro_timer_get_breaks_completed(const PomodoroTimer *timer)
{
  return timer ? timer->breaks_completed : 0;
}

void
pomodoro_timer_start(PomodoroTimer *timer)
{
  if (timer == NULL) {
    return;
  }

  if (timer->state == POMODORO_TIMER_RUNNING) {
    return;
  }

  if (timer->remaining_seconds <= 0) {
    timer->remaining_seconds = pomodoro_timer_phase_seconds(timer, timer->phase);
  }

  timer->state = POMODORO_TIMER_RUNNING;

  if (timer->tick_source_id == 0) {
    timer->tick_source_id = g_timeout_add_seconds(1, pomodoro_timer_on_tick, timer);
  }

  pomodoro_timer_fire_tick(timer);
}

void
pomodoro_timer_pause(PomodoroTimer *timer)
{
  if (timer == NULL) {
    return;
  }

  if (timer->state != POMODORO_TIMER_RUNNING) {
    return;
  }

  timer->state = POMODORO_TIMER_PAUSED;
  pomodoro_timer_stop_tick(timer);
  pomodoro_timer_fire_tick(timer);
}

void
pomodoro_timer_toggle(PomodoroTimer *timer)
{
  if (timer == NULL) {
    return;
  }

  if (timer->state == POMODORO_TIMER_RUNNING) {
    pomodoro_timer_pause(timer);
  } else {
    pomodoro_timer_start(timer);
  }
}

void
pomodoro_timer_skip(PomodoroTimer *timer)
{
  if (timer == NULL) {
    return;
  }

  PomodoroTimerState previous_state = timer->state;
  pomodoro_timer_advance_phase(timer);
  pomodoro_timer_fire_phase(timer);

  if (previous_state == POMODORO_TIMER_RUNNING && timer->tick_source_id == 0) {
    timer->tick_source_id = g_timeout_add_seconds(1, pomodoro_timer_on_tick, timer);
  }

  pomodoro_timer_fire_tick(timer);
}
