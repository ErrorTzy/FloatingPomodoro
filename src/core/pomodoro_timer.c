#include "core/pomodoro_timer.h"

struct _PomodoroTimer {
  PomodoroTimerConfig config;
  PomodoroPhase phase;
  PomodoroTimerState state;
  gint64 remaining_ms;
  guint focus_sessions_completed;
  guint breaks_completed;
  gint64 focus_ms_total;
  gint64 break_ms_total;
  gboolean use_test_durations;
  gint64 focus_ms_override;
  gint64 short_break_ms_override;
  gint64 long_break_ms_override;
  guint tick_interval_ms;
  guint tick_source_id;
  PomodoroTimerUpdateFn tick_cb;
  PomodoroTimerUpdateFn phase_cb;
  gpointer user_data;
};

static gint64
pomodoro_timer_phase_duration_ms(const PomodoroTimer *timer, PomodoroPhase phase)
{
  if (timer == NULL) {
    return 0;
  }

  if (timer->use_test_durations) {
    switch (phase) {
      case POMODORO_PHASE_SHORT_BREAK:
        return timer->short_break_ms_override;
      case POMODORO_PHASE_LONG_BREAK:
        return timer->long_break_ms_override;
      case POMODORO_PHASE_FOCUS:
      default:
        return timer->focus_ms_override;
    }
  }

  switch (phase) {
    case POMODORO_PHASE_SHORT_BREAK:
      return (gint64)timer->config.short_break_minutes * 60 * 1000;
    case POMODORO_PHASE_LONG_BREAK:
      return (gint64)timer->config.long_break_minutes * 60 * 1000;
    case POMODORO_PHASE_FOCUS:
    default:
      return (gint64)timer->config.focus_minutes * 60 * 1000;
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

  timer->remaining_ms = pomodoro_timer_phase_duration_ms(timer, timer->phase);
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

  gint64 tick_ms = (gint64)timer->tick_interval_ms;
  if (tick_ms < 1) {
    tick_ms = 1000;
  }

  if (timer->remaining_ms > 0) {
    timer->remaining_ms -= tick_ms;
  }

  if (timer->phase == POMODORO_PHASE_FOCUS) {
    timer->focus_ms_total += tick_ms;
  } else {
    timer->break_ms_total += tick_ms;
  }

  if (timer->remaining_ms <= 0) {
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
  timer->tick_interval_ms = 1000;
  timer->remaining_ms = pomodoro_timer_phase_duration_ms(timer, timer->phase);
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

  gint64 phase_ms = pomodoro_timer_phase_duration_ms(timer, timer->phase);
  if (timer->state != POMODORO_TIMER_RUNNING) {
    timer->remaining_ms = phase_ms;
  } else if (timer->remaining_ms > phase_ms) {
    timer->remaining_ms = phase_ms;
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

void
pomodoro_timer_set_test_durations(PomodoroTimer *timer,
                                  gint64 focus_ms,
                                  gint64 short_break_ms,
                                  gint64 long_break_ms,
                                  guint tick_interval_ms)
{
  if (timer == NULL) {
    return;
  }

  timer->use_test_durations = TRUE;
  timer->focus_ms_override = focus_ms;
  timer->short_break_ms_override = short_break_ms;
  timer->long_break_ms_override = long_break_ms;
  if (tick_interval_ms > 0) {
    timer->tick_interval_ms = tick_interval_ms;
  }

  if (timer->state != POMODORO_TIMER_RUNNING) {
    timer->remaining_ms = pomodoro_timer_phase_duration_ms(timer, timer->phase);
  }

  if (timer->state == POMODORO_TIMER_RUNNING) {
    pomodoro_timer_stop_tick(timer);
    timer->tick_source_id =
        g_timeout_add(timer->tick_interval_ms, pomodoro_timer_on_tick, timer);
  }

  pomodoro_timer_fire_tick(timer);
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
  if (timer == NULL) {
    return 0;
  }

  if (timer->remaining_ms <= 0) {
    return 0;
  }

  return (timer->remaining_ms + 999) / 1000;
}

gint64
pomodoro_timer_get_phase_total_seconds(const PomodoroTimer *timer,
                                       PomodoroPhase phase)
{
  if (timer == NULL) {
    return 0;
  }

  gint64 duration_ms = pomodoro_timer_phase_duration_ms(timer, phase);
  if (duration_ms <= 0) {
    return 0;
  }

  return (duration_ms + 999) / 1000;
}

gint64
pomodoro_timer_get_focus_seconds(const PomodoroTimer *timer)
{
  return timer ? timer->focus_ms_total / 1000 : 0;
}

gint64
pomodoro_timer_get_break_seconds(const PomodoroTimer *timer)
{
  return timer ? timer->break_ms_total / 1000 : 0;
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

  if (timer->remaining_ms <= 0) {
    timer->remaining_ms = pomodoro_timer_phase_duration_ms(timer, timer->phase);
  }

  timer->state = POMODORO_TIMER_RUNNING;

  if (timer->tick_source_id == 0) {
    timer->tick_source_id =
        g_timeout_add(timer->tick_interval_ms, pomodoro_timer_on_tick, timer);
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
    timer->tick_source_id =
        g_timeout_add(timer->tick_interval_ms, pomodoro_timer_on_tick, timer);
  }

  pomodoro_timer_fire_tick(timer);
}

void
pomodoro_timer_stop(PomodoroTimer *timer)
{
  if (timer == NULL) {
    return;
  }

  pomodoro_timer_stop_tick(timer);
  timer->state = POMODORO_TIMER_STOPPED;
  timer->phase = POMODORO_PHASE_FOCUS;
  timer->remaining_ms = pomodoro_timer_phase_duration_ms(timer, timer->phase);
  timer->focus_sessions_completed = 0;
  timer->breaks_completed = 0;
  timer->focus_ms_total = 0;
  timer->break_ms_total = 0;

  pomodoro_timer_fire_tick(timer);
}
