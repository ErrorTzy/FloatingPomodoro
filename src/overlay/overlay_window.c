#include "overlay/overlay_window.h"

#include <math.h>

#include "core/pomodoro_timer.h"
#include "core/task_store.h"
#include "utils/x11.h"

#define OVERLAY_INFO_REVEAL_DURATION_MS 220

typedef struct {
  AppState *state;
  GtkWindow *window;
  GtkWidget *root;
  GtkWidget *drawing_area;
  GtkWidget *time_label;
  GtkWidget *phase_label;
  GtkWidget *info_revealer;
  GtkWidget *current_task_label;
  GtkWidget *next_task_label;
  GtkWidget *opacity_scale;
  GtkWidget *menu_popover;
  GtkWidget *menu_toggle_button;
  GtkWidget *menu_skip_button;
  GtkWidget *menu_stop_button;
  gboolean menu_open;
  gdouble progress;
  gdouble opacity;
  PomodoroPhase phase;
  PomodoroTimerState timer_state;
} OverlayWindow;

static OverlayWindow *
overlay_from_state(AppState *state)
{
  if (state == NULL || state->overlay_window == NULL) {
    return NULL;
  }

  return g_object_get_data(G_OBJECT(state->overlay_window), "overlay-window");
}

static void
overlay_window_free(gpointer data)
{
  OverlayWindow *overlay = data;
  if (overlay == NULL) {
    return;
  }

  g_free(overlay);
}

gboolean
overlay_window_is_visible(AppState *state)
{
  if (state == NULL || state->overlay_window == NULL) {
    return FALSE;
  }

  return gtk_widget_get_visible(GTK_WIDGET(state->overlay_window));
}

static void
overlay_window_update_toggle_icon(AppState *state)
{
  if (state == NULL || state->overlay_toggle_icon == NULL) {
    return;
  }

  gboolean visible = overlay_window_is_visible(state);
  const char *icon_name = visible ? "pomodoro-overlay-hide-symbolic"
                                  : "pomodoro-overlay-show-symbolic";
  gtk_image_set_from_icon_name(GTK_IMAGE(state->overlay_toggle_icon), icon_name);

  if (state->overlay_toggle_button != NULL) {
    gtk_widget_set_tooltip_text(state->overlay_toggle_button,
                                visible ? "Hide floating ball"
                                        : "Show floating ball");
  }
}

void
overlay_window_sync_toggle_icon(AppState *state)
{
  overlay_window_update_toggle_icon(state);
}

static char *
format_timer_value(gint64 seconds)
{
  if (seconds < 0) {
    seconds = 0;
  }

  gint64 minutes = seconds / 60;
  gint64 secs = seconds % 60;
  return g_strdup_printf("%02" G_GINT64_FORMAT ":%02" G_GINT64_FORMAT,
                         minutes,
                         secs);
}

static void
overlay_set_info_revealed(OverlayWindow *overlay, gboolean reveal, gboolean animate)
{
  if (overlay == NULL || overlay->info_revealer == NULL) {
    return;
  }

  gtk_revealer_set_transition_duration(
      GTK_REVEALER(overlay->info_revealer),
      animate ? OVERLAY_INFO_REVEAL_DURATION_MS : 0);
  gtk_revealer_set_reveal_child(GTK_REVEALER(overlay->info_revealer), reveal);
}

static gboolean
overlay_pointer_inside_root(OverlayWindow *overlay)
{
  if (overlay == NULL || overlay->window == NULL || overlay->root == NULL) {
    return FALSE;
  }

  GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(overlay->window));
  if (surface == NULL) {
    return FALSE;
  }

  GdkDisplay *display = gdk_surface_get_display(surface);
  if (display == NULL) {
    return FALSE;
  }

  GdkSeat *seat = gdk_display_get_default_seat(display);
  if (seat == NULL) {
    return FALSE;
  }

  GdkDevice *device = gdk_seat_get_pointer(seat);
  if (device == NULL) {
    return FALSE;
  }

  double x = 0.0;
  double y = 0.0;
  if (!gdk_surface_get_device_position(surface, device, &x, &y, NULL)) {
    return FALSE;
  }

  graphene_point_t point = {x, y};
  graphene_point_t local = {0};
  if (!gtk_widget_compute_point(GTK_WIDGET(overlay->window),
                                overlay->root,
                                &point,
                                &local)) {
    return FALSE;
  }

  return gtk_widget_contains(overlay->root, local.x, local.y);
}

static void
overlay_sync_hover_state(OverlayWindow *overlay)
{
  if (overlay == NULL || overlay->menu_open) {
    return;
  }

  overlay_set_info_revealed(overlay, overlay_pointer_inside_root(overlay), TRUE);
}

static const char *
phase_title(PomodoroPhase phase)
{
  switch (phase) {
    case POMODORO_PHASE_SHORT_BREAK:
      return "Short Break";
    case POMODORO_PHASE_LONG_BREAK:
      return "Long Break";
    case POMODORO_PHASE_FOCUS:
    default:
      return "Focus";
  }
}

static const char *
phase_action(PomodoroPhase phase)
{
  switch (phase) {
    case POMODORO_PHASE_SHORT_BREAK:
      return "Start Break";
    case POMODORO_PHASE_LONG_BREAK:
      return "Start Long Break";
    case POMODORO_PHASE_FOCUS:
    default:
      return "Start Focus";
  }
}

static const char *
phase_label_for_state(PomodoroTimerState state, PomodoroPhase phase)
{
  if (state == POMODORO_TIMER_PAUSED) {
    return "Paused";
  }

  if (state == POMODORO_TIMER_STOPPED) {
    return "Ready";
  }

  return phase_title(phase);
}

static void
overlay_set_phase_class(OverlayWindow *overlay)
{
  if (overlay == NULL || overlay->root == NULL) {
    return;
  }

  gtk_widget_remove_css_class(overlay->root, "overlay-focus");
  gtk_widget_remove_css_class(overlay->root, "overlay-break");
  gtk_widget_remove_css_class(overlay->root, "overlay-paused");

  if (overlay->timer_state == POMODORO_TIMER_PAUSED ||
      overlay->timer_state == POMODORO_TIMER_STOPPED) {
    gtk_widget_add_css_class(overlay->root, "overlay-paused");
    return;
  }

  if (overlay->phase == POMODORO_PHASE_FOCUS) {
    gtk_widget_add_css_class(overlay->root, "overlay-focus");
  } else {
    gtk_widget_add_css_class(overlay->root, "overlay-break");
  }
}

static void
overlay_draw(GtkDrawingArea *area,
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

  cairo_set_line_width(cr, ring_width);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  gdk_cairo_set_source_rgba(cr, &ring_track);
  cairo_arc(cr, cx, cy, radius - ring_width * 0.5, 0, 2 * G_PI);
  cairo_stroke(cr);

  if (overlay->progress > 0.001) {
    GdkRGBA ring_color = ring_focus;
    if (overlay->timer_state == POMODORO_TIMER_PAUSED ||
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

static void
overlay_set_opacity(OverlayWindow *overlay, gdouble value)
{
  if (overlay == NULL || overlay->root == NULL) {
    return;
  }

  if (value < 0.2) {
    value = 0.2;
  } else if (value > 1.0) {
    value = 1.0;
  }

  overlay->opacity = value;
  gtk_widget_set_opacity(overlay->root, overlay->opacity);
}

static void
on_opacity_changed(GtkRange *range, gpointer user_data)
{
  OverlayWindow *overlay = user_data;
  if (overlay == NULL || range == NULL) {
    return;
  }

  overlay_set_opacity(overlay, gtk_range_get_value(range));
}

static void
on_overlay_pointer_enter(GtkEventControllerMotion *controller,
                         gdouble x,
                         gdouble y,
                         gpointer user_data)
{
  (void)controller;
  (void)x;
  (void)y;
  OverlayWindow *overlay = user_data;
  if (overlay == NULL || overlay->info_revealer == NULL) {
    return;
  }

  if (overlay->menu_open) {
    return;
  }

  overlay_set_info_revealed(overlay, TRUE, TRUE);
}

static void
on_overlay_pointer_leave(GtkEventControllerMotion *controller,
                         gpointer user_data)
{
  (void)controller;
  OverlayWindow *overlay = user_data;
  if (overlay == NULL || overlay->info_revealer == NULL) {
    return;
  }

  if (overlay->menu_open) {
    return;
  }

  overlay_set_info_revealed(overlay, FALSE, TRUE);
}

static void
on_overlay_drag_begin(GtkGestureDrag *gesture,
                      gdouble start_x,
                      gdouble start_y,
                      gpointer user_data)
{
  OverlayWindow *overlay = user_data;
  if (overlay == NULL || overlay->window == NULL) {
    return;
  }

  GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(overlay->window));
  if (surface == NULL || !GDK_IS_TOPLEVEL(surface)) {
    return;
  }

  GdkDevice *device = gtk_gesture_get_device(GTK_GESTURE(gesture));
  guint32 time = gtk_event_controller_get_current_event_time(
      GTK_EVENT_CONTROLLER(gesture));
  gdk_toplevel_begin_move(GDK_TOPLEVEL(surface), device, 1, start_x, start_y, time);
}

static void
on_menu_popover_closed(GtkPopover *popover, gpointer user_data)
{
  (void)popover;
  OverlayWindow *overlay = user_data;
  if (overlay == NULL) {
    return;
  }

  overlay->menu_open = FALSE;
  overlay_sync_hover_state(overlay);
}

static void
overlay_pop_menu(OverlayWindow *overlay, gdouble x, gdouble y)
{
  if (overlay == NULL || overlay->menu_popover == NULL || overlay->root == NULL) {
    return;
  }

  overlay->menu_open = TRUE;
  overlay_set_info_revealed(overlay, FALSE, FALSE);

  GdkRectangle rect = {(int)x, (int)y, 1, 1};
  gtk_popover_set_pointing_to(GTK_POPOVER(overlay->menu_popover), &rect);
  gtk_popover_popup(GTK_POPOVER(overlay->menu_popover));
}

static void
on_overlay_right_click(GtkGestureClick *gesture,
                       gint n_press,
                       gdouble x,
                       gdouble y,
                       gpointer user_data)
{
  (void)gesture;
  if (n_press != 1) {
    return;
  }

  overlay_pop_menu((OverlayWindow *)user_data, x, y);
}

static void
overlay_menu_popdown(OverlayWindow *overlay)
{
  if (overlay == NULL || overlay->menu_popover == NULL) {
    return;
  }

  overlay->menu_open = FALSE;
  gtk_popover_popdown(GTK_POPOVER(overlay->menu_popover));
}

static void
on_menu_toggle_clicked(GtkButton *button, gpointer user_data)
{
  (void)button;
  OverlayWindow *overlay = user_data;
  if (overlay == NULL || overlay->state == NULL || overlay->state->timer == NULL) {
    return;
  }

  pomodoro_timer_toggle(overlay->state->timer);
  overlay_menu_popdown(overlay);
}

static void
on_menu_skip_clicked(GtkButton *button, gpointer user_data)
{
  (void)button;
  OverlayWindow *overlay = user_data;
  if (overlay == NULL || overlay->state == NULL || overlay->state->timer == NULL) {
    return;
  }

  pomodoro_timer_skip(overlay->state->timer);
  overlay_menu_popdown(overlay);
}

static void
on_menu_stop_clicked(GtkButton *button, gpointer user_data)
{
  (void)button;
  OverlayWindow *overlay = user_data;
  if (overlay == NULL || overlay->state == NULL || overlay->state->timer == NULL) {
    return;
  }

  pomodoro_timer_stop(overlay->state->timer);
  overlay_menu_popdown(overlay);
}

static void
on_menu_hide_clicked(GtkButton *button, gpointer user_data)
{
  (void)button;
  OverlayWindow *overlay = user_data;
  if (overlay == NULL || overlay->state == NULL) {
    return;
  }

  overlay_menu_popdown(overlay);
  overlay_window_set_visible(overlay->state, FALSE);
}

static void
on_menu_show_clicked(GtkButton *button, gpointer user_data)
{
  (void)button;
  OverlayWindow *overlay = user_data;
  if (overlay == NULL || overlay->state == NULL || overlay->state->window == NULL) {
    return;
  }

  gtk_window_present(overlay->state->window);
  overlay_menu_popdown(overlay);
}

static void
on_menu_quit_clicked(GtkButton *button, gpointer user_data)
{
  (void)button;
  OverlayWindow *overlay = user_data;
  if (overlay == NULL || overlay->window == NULL) {
    return;
  }

  GtkApplication *app = gtk_window_get_application(overlay->window);
  if (app != NULL) {
    g_application_quit(G_APPLICATION(app));
  }
}

static GtkWidget *
create_menu_button(const char *label, const char *css_class)
{
  GtkWidget *button = gtk_button_new_with_label(label);
  gtk_widget_add_css_class(button, "overlay-menu-button");
  if (css_class != NULL) {
    gtk_widget_add_css_class(button, css_class);
  }
  gtk_widget_set_halign(button, GTK_ALIGN_FILL);
  return button;
}

static PomodoroTask *
find_next_task(TaskStore *store, PomodoroTask *active)
{
  if (store == NULL) {
    return NULL;
  }

  const GPtrArray *tasks = task_store_get_tasks(store);
  if (tasks == NULL) {
    return NULL;
  }

  for (guint i = 0; i < tasks->len; i++) {
    PomodoroTask *task = g_ptr_array_index((GPtrArray *)tasks, i);
    if (task == NULL || task == active) {
      continue;
    }

    TaskStatus status = pomodoro_task_get_status(task);
    if (status == TASK_STATUS_PENDING) {
      return task;
    }
  }

  return NULL;
}

static gboolean
overlay_apply_x11_hints_idle(gpointer data)
{
  OverlayWindow *overlay = data;
  if (overlay == NULL || overlay->window == NULL) {
    return G_SOURCE_REMOVE;
  }

  x11_window_set_keep_above(overlay->window, TRUE);
  x11_window_set_skip_taskbar(overlay->window, TRUE);
  x11_window_set_skip_pager(overlay->window, TRUE);
  return G_SOURCE_REMOVE;
}

void
overlay_window_create(GtkApplication *app, AppState *state)
{
  if (app == NULL || state == NULL) {
    return;
  }

  if (state->overlay_window != NULL) {
    return;
  }

  GtkWidget *window = gtk_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(window), "Pomodoro Overlay");
  gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
  gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
  gtk_window_set_default_size(GTK_WINDOW(window), 140, 140);
  gtk_window_set_focus_visible(GTK_WINDOW(window), FALSE);
  gtk_window_set_deletable(GTK_WINDOW(window), FALSE);
  gtk_widget_add_css_class(window, "overlay-window");

  OverlayWindow *overlay = g_new0(OverlayWindow, 1);
  overlay->state = state;
  overlay->window = GTK_WINDOW(window);
  overlay->opacity = 0.65;
  overlay->phase = POMODORO_PHASE_FOCUS;
  overlay->timer_state = POMODORO_TIMER_STOPPED;
  overlay->progress = 0.0;

  g_object_set_data_full(G_OBJECT(window),
                         "overlay-window",
                         overlay,
                         overlay_window_free);

  state->overlay_window = GTK_WINDOW(window);
  g_object_add_weak_pointer(G_OBJECT(window),
                            (gpointer *)&state->overlay_window);

  GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_widget_add_css_class(root, "overlay-root");
  gtk_widget_set_margin_top(root, 6);
  gtk_widget_set_margin_bottom(root, 6);
  gtk_widget_set_margin_start(root, 6);
  gtk_widget_set_margin_end(root, 6);
  overlay->root = root;
  overlay_set_opacity(overlay, overlay->opacity);

  GtkWidget *bubble = gtk_overlay_new();
  gtk_widget_add_css_class(bubble, "overlay-bubble");
  gtk_widget_set_size_request(bubble, 128, 128);
  gtk_widget_set_hexpand(bubble, TRUE);
  gtk_widget_set_vexpand(bubble, TRUE);

  GtkWidget *bubble_frame =
      gtk_aspect_frame_new(0.5f, 0.0f, 1.0f, FALSE);
  gtk_widget_set_halign(bubble_frame, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(bubble_frame, GTK_ALIGN_START);
  gtk_widget_set_hexpand(bubble_frame, TRUE);
  gtk_widget_set_vexpand(bubble_frame, FALSE);
  gtk_aspect_frame_set_child(GTK_ASPECT_FRAME(bubble_frame), bubble);

  GtkWidget *drawing = gtk_drawing_area_new();
  gtk_widget_set_hexpand(drawing, TRUE);
  gtk_widget_set_vexpand(drawing, TRUE);
  gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(drawing), 128);
  gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(drawing), 128);
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(drawing),
                                 overlay_draw,
                                 overlay,
                                 NULL);
  gtk_overlay_set_child(GTK_OVERLAY(bubble), drawing);
  overlay->drawing_area = drawing;

  GtkWidget *label_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  gtk_widget_set_halign(label_box, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(label_box, GTK_ALIGN_CENTER);
  gtk_widget_add_css_class(label_box, "overlay-text");

  GtkWidget *time_label = gtk_label_new("25:00");
  gtk_widget_add_css_class(time_label, "overlay-time");
  gtk_widget_set_halign(time_label, GTK_ALIGN_CENTER);
  overlay->time_label = time_label;

  GtkWidget *phase_label = gtk_label_new("Focus");
  gtk_widget_add_css_class(phase_label, "overlay-phase");
  gtk_widget_set_halign(phase_label, GTK_ALIGN_CENTER);
  overlay->phase_label = phase_label;

  gtk_box_append(GTK_BOX(label_box), time_label);
  gtk_box_append(GTK_BOX(label_box), phase_label);
  gtk_overlay_add_overlay(GTK_OVERLAY(bubble), label_box);

  GtkWidget *revealer = gtk_revealer_new();
  gtk_revealer_set_transition_type(GTK_REVEALER(revealer),
                                   GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
  gtk_revealer_set_transition_duration(GTK_REVEALER(revealer),
                                       OVERLAY_INFO_REVEAL_DURATION_MS);
  gtk_revealer_set_reveal_child(GTK_REVEALER(revealer), FALSE);
  overlay->info_revealer = revealer;

  GtkWidget *panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_add_css_class(panel, "overlay-panel");

  GtkWidget *current_title = gtk_label_new("Current task");
  gtk_widget_add_css_class(current_title, "overlay-panel-title");
  gtk_widget_set_halign(current_title, GTK_ALIGN_START);

  GtkWidget *current_value = gtk_label_new("No active task");
  gtk_widget_add_css_class(current_value, "overlay-panel-value");
  gtk_label_set_wrap(GTK_LABEL(current_value), TRUE);
  gtk_label_set_ellipsize(GTK_LABEL(current_value), PANGO_ELLIPSIZE_END);
  gtk_widget_set_halign(current_value, GTK_ALIGN_START);
  overlay->current_task_label = current_value;

  GtkWidget *next_title = gtk_label_new("Next task");
  gtk_widget_add_css_class(next_title, "overlay-panel-title");
  gtk_widget_set_halign(next_title, GTK_ALIGN_START);

  GtkWidget *next_value = gtk_label_new("Pick one from the list");
  gtk_widget_add_css_class(next_value, "overlay-panel-value");
  gtk_label_set_wrap(GTK_LABEL(next_value), TRUE);
  gtk_label_set_ellipsize(GTK_LABEL(next_value), PANGO_ELLIPSIZE_END);
  gtk_widget_set_halign(next_value, GTK_ALIGN_START);
  overlay->next_task_label = next_value;

  GtkWidget *opacity_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_halign(opacity_row, GTK_ALIGN_FILL);

  GtkWidget *opacity_label = gtk_label_new("Opacity");
  gtk_widget_add_css_class(opacity_label, "overlay-panel-meta");
  gtk_widget_set_halign(opacity_label, GTK_ALIGN_START);

  GtkWidget *opacity_scale = gtk_scale_new_with_range(
      GTK_ORIENTATION_HORIZONTAL,
      0.3,
      1.0,
      0.01);
  gtk_widget_set_hexpand(opacity_scale, TRUE);
  gtk_scale_set_draw_value(GTK_SCALE(opacity_scale), FALSE);
  gtk_range_set_value(GTK_RANGE(opacity_scale), overlay->opacity);
  gtk_widget_add_css_class(opacity_scale, "overlay-opacity");
  g_signal_connect(opacity_scale,
                   "value-changed",
                   G_CALLBACK(on_opacity_changed),
                   overlay);
  overlay->opacity_scale = opacity_scale;

  gtk_box_append(GTK_BOX(opacity_row), opacity_label);
  gtk_box_append(GTK_BOX(opacity_row), opacity_scale);

  gtk_box_append(GTK_BOX(panel), current_title);
  gtk_box_append(GTK_BOX(panel), current_value);
  gtk_box_append(GTK_BOX(panel), next_title);
  gtk_box_append(GTK_BOX(panel), next_value);
  gtk_box_append(GTK_BOX(panel), opacity_row);

  gtk_revealer_set_child(GTK_REVEALER(revealer), panel);

  GtkWidget *menu_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
  gtk_widget_add_css_class(menu_box, "overlay-menu");

  GtkWidget *toggle_button = create_menu_button("Start Focus", "btn-primary");
  g_signal_connect(toggle_button,
                   "clicked",
                   G_CALLBACK(on_menu_toggle_clicked),
                   overlay);
  overlay->menu_toggle_button = toggle_button;

  GtkWidget *skip_button = create_menu_button("Skip", "btn-secondary");
  g_signal_connect(skip_button,
                   "clicked",
                   G_CALLBACK(on_menu_skip_clicked),
                   overlay);
  overlay->menu_skip_button = skip_button;

  GtkWidget *stop_button = create_menu_button("Stop", "btn-danger");
  g_signal_connect(stop_button,
                   "clicked",
                   G_CALLBACK(on_menu_stop_clicked),
                   overlay);
  overlay->menu_stop_button = stop_button;

  GtkWidget *hide_button = create_menu_button("Hide", "btn-secondary");
  g_signal_connect(hide_button,
                   "clicked",
                   G_CALLBACK(on_menu_hide_clicked),
                   overlay);

  GtkWidget *show_button = create_menu_button("Open App", "btn-secondary");
  g_signal_connect(show_button,
                   "clicked",
                   G_CALLBACK(on_menu_show_clicked),
                   overlay);

  GtkWidget *quit_button = create_menu_button("Quit", "btn-secondary");
  g_signal_connect(quit_button,
                   "clicked",
                   G_CALLBACK(on_menu_quit_clicked),
                   overlay);

  gtk_box_append(GTK_BOX(menu_box), toggle_button);
  gtk_box_append(GTK_BOX(menu_box), skip_button);
  gtk_box_append(GTK_BOX(menu_box), stop_button);
  gtk_box_append(GTK_BOX(menu_box), hide_button);
  gtk_box_append(GTK_BOX(menu_box), show_button);
  gtk_box_append(GTK_BOX(menu_box), quit_button);

  GtkWidget *popover = gtk_popover_new();
  gtk_popover_set_has_arrow(GTK_POPOVER(popover), FALSE);
  gtk_popover_set_autohide(GTK_POPOVER(popover), TRUE);
  g_signal_connect(popover,
                   "closed",
                   G_CALLBACK(on_menu_popover_closed),
                   overlay);
  gtk_popover_set_child(GTK_POPOVER(popover), menu_box);
  gtk_widget_add_css_class(popover, "overlay-menu-popover");
  gtk_widget_set_parent(popover, root);
  overlay->menu_popover = popover;

  GtkEventController *motion = gtk_event_controller_motion_new();
  g_signal_connect(motion,
                   "enter",
                   G_CALLBACK(on_overlay_pointer_enter),
                   overlay);
  g_signal_connect(motion,
                   "leave",
                   G_CALLBACK(on_overlay_pointer_leave),
                   overlay);
  gtk_widget_add_controller(root, motion);

  GtkGesture *drag = gtk_gesture_drag_new();
  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(drag), GDK_BUTTON_PRIMARY);
  g_signal_connect(drag,
                   "drag-begin",
                   G_CALLBACK(on_overlay_drag_begin),
                   overlay);
  gtk_widget_add_controller(bubble, GTK_EVENT_CONTROLLER(drag));

  GtkGesture *right_click = gtk_gesture_click_new();
  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(right_click),
                                GDK_BUTTON_SECONDARY);
  g_signal_connect(right_click,
                   "pressed",
                   G_CALLBACK(on_overlay_right_click),
                   overlay);
  gtk_widget_add_controller(root, GTK_EVENT_CONTROLLER(right_click));

  gtk_box_append(GTK_BOX(root), bubble_frame);
  gtk_box_append(GTK_BOX(root), revealer);

  gtk_window_set_child(GTK_WINDOW(window), root);

  gtk_window_present(GTK_WINDOW(window));
  g_idle_add(overlay_apply_x11_hints_idle, overlay);

  overlay_window_update(state);
}

void
overlay_window_update(AppState *state)
{
  OverlayWindow *overlay = overlay_from_state(state);
  if (overlay == NULL || state == NULL || state->timer == NULL) {
    return;
  }

  PomodoroTimer *timer = state->timer;
  overlay->timer_state = pomodoro_timer_get_state(timer);
  overlay->phase = pomodoro_timer_get_phase(timer);

  gint64 remaining_seconds = pomodoro_timer_get_remaining_seconds(timer);
  gint64 total_seconds =
      pomodoro_timer_get_phase_total_seconds(timer, overlay->phase);
  if (total_seconds < 1) {
    total_seconds = 1;
  }

  gdouble progress = 1.0 - ((gdouble)remaining_seconds / (gdouble)total_seconds);
  if (overlay->timer_state == POMODORO_TIMER_STOPPED) {
    progress = 0.0;
  }

  if (progress < 0.0) {
    progress = 0.0;
  } else if (progress > 1.0) {
    progress = 1.0;
  }

  overlay->progress = progress;

  if (overlay->time_label != NULL) {
    char *time_text = format_timer_value(remaining_seconds);
    gtk_label_set_text(GTK_LABEL(overlay->time_label), time_text);
    g_free(time_text);
  }

  if (overlay->phase_label != NULL) {
    gtk_label_set_text(GTK_LABEL(overlay->phase_label),
                       phase_label_for_state(overlay->timer_state,
                                             overlay->phase));
  }

  PomodoroTask *active_task = task_store_get_active(state->store);
  PomodoroTask *next_task = find_next_task(state->store, active_task);

  if (overlay->current_task_label != NULL) {
    const char *title = active_task ? pomodoro_task_get_title(active_task)
                                    : "No active task";
    gtk_label_set_text(GTK_LABEL(overlay->current_task_label), title);
    gtk_widget_set_tooltip_text(overlay->current_task_label, title);
  }

  if (overlay->next_task_label != NULL) {
    const char *next_title = next_task ? pomodoro_task_get_title(next_task)
                                       : "Pick one from the list";
    gtk_label_set_text(GTK_LABEL(overlay->next_task_label), next_title);
    gtk_widget_set_tooltip_text(overlay->next_task_label, next_title);
  }

  if (overlay->menu_toggle_button != NULL) {
    const char *label = NULL;
    if (overlay->timer_state == POMODORO_TIMER_RUNNING) {
      label = "Pause";
    } else if (overlay->timer_state == POMODORO_TIMER_PAUSED) {
      label = "Resume";
    } else {
      label = phase_action(overlay->phase);
    }
    gtk_button_set_label(GTK_BUTTON(overlay->menu_toggle_button), label);
  }

  gboolean has_task = active_task != NULL;
  if (overlay->menu_skip_button != NULL) {
    gtk_widget_set_sensitive(overlay->menu_skip_button,
                             has_task &&
                                 overlay->timer_state != POMODORO_TIMER_STOPPED);
  }

  if (overlay->menu_stop_button != NULL) {
    gtk_widget_set_sensitive(overlay->menu_stop_button,
                             has_task &&
                                 overlay->timer_state != POMODORO_TIMER_STOPPED);
  }

  overlay_set_phase_class(overlay);

  if (overlay->drawing_area != NULL) {
    gtk_widget_queue_draw(overlay->drawing_area);
  }
}

void
overlay_window_set_visible(AppState *state, gboolean visible)
{
  if (state == NULL || state->overlay_window == NULL) {
    return;
  }

  gboolean is_visible = overlay_window_is_visible(state);
  if (visible == is_visible) {
    overlay_window_update_toggle_icon(state);
    return;
  }

  OverlayWindow *overlay = overlay_from_state(state);
  if (visible) {
    gtk_widget_set_visible(GTK_WIDGET(state->overlay_window), TRUE);
    gtk_window_present(state->overlay_window);
    if (overlay != NULL) {
      g_idle_add(overlay_apply_x11_hints_idle, overlay);
      overlay_sync_hover_state(overlay);
    }
  } else {
    if (overlay != NULL) {
      overlay_set_info_revealed(overlay, FALSE, FALSE);
      if (overlay->menu_open) {
        overlay_menu_popdown(overlay);
      }
    }
    gtk_widget_set_visible(GTK_WIDGET(state->overlay_window), FALSE);
  }

  overlay_window_update_toggle_icon(state);
}

void
overlay_window_toggle_visible(AppState *state)
{
  if (state == NULL) {
    return;
  }

  overlay_window_set_visible(state, !overlay_window_is_visible(state));
}
