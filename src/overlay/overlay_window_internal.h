#pragma once

#include <gtk/gtk.h>

#include "app/app_state.h"
#include "core/pomodoro_timer.h"

#define OVERLAY_INFO_REVEAL_DURATION_MS 220

typedef struct _OverlayWindow OverlayWindow;

struct _OverlayWindow {
  AppState *state;
  GtkWindow *window;
  GtkWidget *root;
  GtkWidget *bubble;
  GtkWidget *bubble_frame;
  GtkWidget *drawing_area;
  GtkWidget *time_label;
  GtkWidget *phase_label;
  GtkWidget *warning_box;
  GtkWidget *warning_title_label;
  GtkWidget *warning_focus_label;
  GtkWidget *warning_app_label;
  GtkWidget *info_revealer;
  GtkWidget *current_task_label;
  GtkWidget *next_task_label;
  GtkWidget *opacity_scale;
  GtkWidget *menu_popover;
  GtkWidget *menu_toggle_button;
  GtkWidget *menu_toggle_icon;
  GtkWidget *menu_skip_button;
  GtkWidget *menu_stop_button;
  GtkWidget *menu_hide_button;
  GtkWidget *menu_show_button;
  GtkWidget *menu_quit_button;
  gboolean menu_open;
  gdouble progress;
  gdouble opacity;
  PomodoroPhase phase;
  PomodoroTimerState timer_state;
  gboolean warning_active;
};

GtkWindow *overlay_window_create_window(GtkApplication *app);
void overlay_window_build_ui(OverlayWindow *overlay);

void overlay_window_set_info_revealed(OverlayWindow *overlay,
                                      gboolean reveal,
                                      gboolean animate);
void overlay_window_sync_hover_state(OverlayWindow *overlay);
void overlay_window_menu_popdown(OverlayWindow *overlay);
void overlay_window_set_phase_class(OverlayWindow *overlay);
void overlay_window_set_opacity(OverlayWindow *overlay, gdouble value);
void overlay_window_bind_actions(OverlayWindow *overlay, GtkWidget *bubble);
void overlay_window_update_input_region(OverlayWindow *overlay);

void overlay_window_draw(GtkDrawingArea *area,
                         cairo_t *cr,
                         int width,
                         int height,
                         gpointer user_data);
