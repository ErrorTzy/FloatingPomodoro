#pragma once

#include <glib.h>

typedef enum {
  POMODORO_PHASE_FOCUS = 0,
  POMODORO_PHASE_SHORT_BREAK = 1,
  POMODORO_PHASE_LONG_BREAK = 2
} PomodoroPhase;

typedef enum {
  POMODORO_TIMER_STOPPED = 0,
  POMODORO_TIMER_RUNNING = 1,
  POMODORO_TIMER_PAUSED = 2
} PomodoroTimerState;

typedef struct {
  guint focus_minutes;
  guint short_break_minutes;
  guint long_break_minutes;
  guint long_break_interval;
} PomodoroTimerConfig;

typedef struct _PomodoroTimer PomodoroTimer;
typedef void (*PomodoroTimerUpdateFn)(PomodoroTimer *timer, gpointer user_data);

PomodoroTimerConfig pomodoro_timer_config_default(void);
PomodoroTimerConfig pomodoro_timer_config_normalize(PomodoroTimerConfig config);

PomodoroTimer *pomodoro_timer_new(PomodoroTimerConfig config);
void pomodoro_timer_free(PomodoroTimer *timer);

void pomodoro_timer_set_update_callback(PomodoroTimer *timer,
                                        PomodoroTimerUpdateFn tick_cb,
                                        PomodoroTimerUpdateFn phase_cb,
                                        gpointer user_data);

void pomodoro_timer_apply_config(PomodoroTimer *timer, PomodoroTimerConfig config);
PomodoroTimerConfig pomodoro_timer_get_config(const PomodoroTimer *timer);

PomodoroPhase pomodoro_timer_get_phase(const PomodoroTimer *timer);
PomodoroPhase pomodoro_timer_get_next_phase(const PomodoroTimer *timer);
PomodoroTimerState pomodoro_timer_get_state(const PomodoroTimer *timer);
gint64 pomodoro_timer_get_remaining_seconds(const PomodoroTimer *timer);

gint64 pomodoro_timer_get_focus_seconds(const PomodoroTimer *timer);
gint64 pomodoro_timer_get_break_seconds(const PomodoroTimer *timer);
guint pomodoro_timer_get_focus_sessions_completed(const PomodoroTimer *timer);
guint pomodoro_timer_get_breaks_completed(const PomodoroTimer *timer);

void pomodoro_timer_start(PomodoroTimer *timer);
void pomodoro_timer_pause(PomodoroTimer *timer);
void pomodoro_timer_toggle(PomodoroTimer *timer);
void pomodoro_timer_skip(PomodoroTimer *timer);
