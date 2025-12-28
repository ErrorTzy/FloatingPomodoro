#pragma once

#include <gtk/gtk.h>

#include "app/app_state.h"
#include "core/pomodoro_timer.h"

void main_window_build_ui(AppState *state, gboolean autostart_launch);

void on_timer_start_clicked(GtkButton *button, gpointer user_data);
void on_timer_skip_clicked(GtkButton *button, gpointer user_data);
void on_timer_stop_clicked(GtkButton *button, gpointer user_data);
void on_overlay_toggle_clicked(GtkButton *button, gpointer user_data);

void on_timer_tick(PomodoroTimer *timer, gpointer user_data);
void on_timer_phase_changed(PomodoroTimer *timer, gpointer user_data);
