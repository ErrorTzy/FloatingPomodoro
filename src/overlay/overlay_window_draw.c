#include "overlay/overlay_window_internal.h"

#include <math.h>

void
overlay_window_draw(GtkDrawingArea *area,
                    cairo_t *cr,
                    int width,
                    int height,
                    gpointer user_data)
{
  (void)area;
  OverlayWindow *overlay = user_data;
  if (overlay == NULL || width <= 0 || height <= 0) {
    return;
  }

  double size = MIN(width, height);
  double radius = (size / 2.0) - 6.0;
  if (radius < 10.0) {
    radius = 10.0;
  }

  double cx = width / 2.0;
  double cy = height / 2.0;
  double ring_width = MAX(6.0, radius * 0.12);

  GdkRGBA base_start = {0.98, 0.95, 0.90, 0.95};
  GdkRGBA base_end = {0.94, 0.90, 0.84, 0.95};
  GdkRGBA ring_track = {0.06, 0.30, 0.36, 0.18};
  GdkRGBA ring_focus = {0.06, 0.30, 0.36, 0.95};
  GdkRGBA ring_break = {0.89, 0.39, 0.08, 0.95};
  GdkRGBA ring_long_break = {0.24, 0.51, 0.38, 0.95};
  GdkRGBA ring_paused = {0.36, 0.36, 0.36, 0.65};
  GdkRGBA ring_warning = {0.98, 0.12, 0.18, 1.0};

  if (overlay->warning_active) {
    ring_width = MAX(7.0, radius * 0.15);
    base_start = (GdkRGBA){1.00, 0.84, 0.86, 0.96};
    base_end = (GdkRGBA){0.96, 0.52, 0.56, 0.96};
    ring_track = (GdkRGBA){0.95, 0.18, 0.24, 0.38};
  }

  cairo_save(cr);
  cairo_arc(cr, cx, cy, radius, 0, 2 * G_PI);
  cairo_clip(cr);

  cairo_pattern_t *gradient = cairo_pattern_create_radial(
      cx - radius * 0.35,
      cy - radius * 0.35,
      radius * 0.15,
      cx,
      cy,
      radius);
  cairo_pattern_add_color_stop_rgba(gradient,
                                    0,
                                    base_start.red,
                                    base_start.green,
                                    base_start.blue,
                                    base_start.alpha);
  cairo_pattern_add_color_stop_rgba(gradient,
                                    1,
                                    base_end.red,
                                    base_end.green,
                                    base_end.blue,
                                    base_end.alpha);
  cairo_set_source(cr, gradient);
  cairo_paint(cr);
  cairo_pattern_destroy(gradient);
  cairo_restore(cr);

  if (overlay->warning_active) {
    cairo_save(cr);
    cairo_arc(cr, cx, cy, radius, 0, 2 * G_PI);
    cairo_clip(cr);
    cairo_set_source_rgba(cr, 0.88, 0.12, 0.18, 0.18);
    cairo_paint(cr);
    cairo_restore(cr);

    GdkRGBA inner_glow = {1.0, 0.32, 0.36, 0.55};
    cairo_set_line_width(cr, ring_width * 0.6);
    gdk_cairo_set_source_rgba(cr, &inner_glow);
    cairo_arc(cr, cx, cy, radius - ring_width * 1.2, 0, 2 * G_PI);
    cairo_stroke(cr);
  }

  cairo_set_line_width(cr, ring_width);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  gdk_cairo_set_source_rgba(cr, &ring_track);
  cairo_arc(cr, cx, cy, radius - ring_width * 0.5, 0, 2 * G_PI);
  cairo_stroke(cr);

  if (overlay->progress > 0.001) {
    GdkRGBA ring_color = ring_focus;
    if (overlay->warning_active) {
      ring_color = ring_warning;
    } else if (overlay->timer_state == POMODORO_TIMER_PAUSED ||
               overlay->timer_state == POMODORO_TIMER_STOPPED) {
      ring_color = ring_paused;
    } else if (overlay->phase == POMODORO_PHASE_SHORT_BREAK) {
      ring_color = ring_break;
    } else if (overlay->phase == POMODORO_PHASE_LONG_BREAK) {
      ring_color = ring_long_break;
    }

    gdk_cairo_set_source_rgba(cr, &ring_color);
    double start_angle = -G_PI / 2.0;
    double end_angle = start_angle + (2 * G_PI * overlay->progress);
    cairo_arc(cr,
              cx,
              cy,
              radius - ring_width * 0.5,
              start_angle,
              end_angle);
    cairo_stroke(cr);
  }
}
