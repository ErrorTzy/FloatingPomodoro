#include "overlay/overlay_window_internal.h"

#include "overlay/overlay_window.h"

#include "core/pomodoro_timer.h"

#include <cairo.h>

static gboolean
overlay_window_pointer_inside_root(OverlayWindow *overlay)
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
overlay_window_union_widget_region(cairo_region_t *region,
                                   GtkWidget *widget,
                                   GtkWidget *window)
{
  if (region == NULL || widget == NULL || window == NULL) {
    return;
  }

  if (!gtk_widget_get_visible(widget)) {
    return;
  }

  int width = gtk_widget_get_width(widget);
  int height = gtk_widget_get_height(widget);
  if (width <= 0 || height <= 0) {
    return;
  }

  graphene_point_t origin = {0};
  graphene_point_t window_point = {0};
  if (!gtk_widget_compute_point(widget, window, &origin, &window_point)) {
    return;
  }

  cairo_rectangle_int_t rect = {(int)window_point.x,
                                (int)window_point.y,
                                width,
                                height};
  cairo_region_union_rectangle(region, &rect);
}

void
overlay_window_update_input_region(OverlayWindow *overlay)
{
  if (overlay == NULL || overlay->window == NULL) {
    return;
  }

  GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(overlay->window));
  if (surface == NULL) {
    return;
  }

  cairo_region_t *region = cairo_region_create();

  overlay_window_union_widget_region(region,
                                     overlay->bubble_frame != NULL
                                         ? overlay->bubble_frame
                                         : overlay->bubble,
                                     GTK_WIDGET(overlay->window));

  if (overlay->info_revealer != NULL) {
    gboolean reveal =
        gtk_revealer_get_reveal_child(GTK_REVEALER(overlay->info_revealer));
    if (reveal || gtk_widget_get_visible(overlay->info_revealer)) {
      overlay_window_union_widget_region(region,
                                         overlay->info_revealer,
                                         GTK_WIDGET(overlay->window));
    }
  }

  if (!cairo_region_is_empty(region)) {
    gdk_surface_set_input_region(surface, region);
  }
  cairo_region_destroy(region);
}

void
overlay_window_set_info_revealed(OverlayWindow *overlay,
                                 gboolean reveal,
                                 gboolean animate)
{
  if (overlay == NULL || overlay->info_revealer == NULL) {
    return;
  }

  gtk_revealer_set_transition_duration(
      GTK_REVEALER(overlay->info_revealer),
      animate ? OVERLAY_INFO_REVEAL_DURATION_MS : 0);
  gtk_revealer_set_reveal_child(GTK_REVEALER(overlay->info_revealer), reveal);

  overlay_window_update_input_region(overlay);
  if (animate) {
    overlay_window_request_size_updates(overlay,
                                        OVERLAY_INFO_REVEAL_DURATION_MS + 80);
  }
}

void
overlay_window_sync_hover_state(OverlayWindow *overlay)
{
  if (overlay == NULL || overlay->menu_open) {
    return;
  }

  overlay_window_set_info_revealed(overlay,
                                   overlay_window_pointer_inside_root(overlay),
                                   TRUE);
}

void
overlay_window_set_phase_class(OverlayWindow *overlay)
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

void
overlay_window_set_opacity(OverlayWindow *overlay, gdouble value)
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

void
overlay_window_menu_popdown(OverlayWindow *overlay)
{
  if (overlay == NULL || overlay->menu_popover == NULL) {
    return;
  }

  overlay->menu_open = FALSE;
  gtk_popover_popdown(GTK_POPOVER(overlay->menu_popover));
}

static void
overlay_window_pop_menu(OverlayWindow *overlay, gdouble x, gdouble y)
{
  if (overlay == NULL || overlay->menu_popover == NULL || overlay->root == NULL) {
    return;
  }

  overlay->menu_open = TRUE;
  overlay_window_set_info_revealed(overlay, FALSE, FALSE);

  GdkRectangle rect = {(int)x, (int)y, 1, 1};
  gtk_popover_set_pointing_to(GTK_POPOVER(overlay->menu_popover), &rect);
  gtk_popover_popup(GTK_POPOVER(overlay->menu_popover));
}

static void
overlay_window_on_opacity_changed(GtkRange *range, gpointer user_data)
{
  OverlayWindow *overlay = user_data;
  if (overlay == NULL || range == NULL) {
    return;
  }

  overlay_window_set_opacity(overlay, gtk_range_get_value(range));
}

static void
overlay_window_on_overlay_pointer_enter(GtkEventControllerMotion *controller,
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

  overlay_window_set_info_revealed(overlay, TRUE, TRUE);
}

static void
overlay_window_on_overlay_pointer_leave(GtkEventControllerMotion *controller,
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

  overlay_window_set_info_revealed(overlay, FALSE, TRUE);
}

static void
overlay_window_on_overlay_drag_begin(GtkGestureDrag *gesture,
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
overlay_window_on_menu_popover_closed(GtkPopover *popover, gpointer user_data)
{
  (void)popover;
  OverlayWindow *overlay = user_data;
  if (overlay == NULL) {
    return;
  }

  overlay->menu_open = FALSE;
  overlay_window_sync_hover_state(overlay);
}

static void
overlay_window_on_overlay_right_click(GtkGestureClick *gesture,
                                     gint n_press,
                                     gdouble x,
                                     gdouble y,
                                     gpointer user_data)
{
  (void)gesture;
  if (n_press != 1) {
    return;
  }

  overlay_window_pop_menu((OverlayWindow *)user_data, x, y);
}

static void
overlay_window_on_menu_toggle_clicked(GtkButton *button, gpointer user_data)
{
  (void)button;
  OverlayWindow *overlay = user_data;
  if (overlay == NULL || overlay->state == NULL || overlay->state->timer == NULL) {
    return;
  }

  pomodoro_timer_toggle(overlay->state->timer);
  overlay_window_menu_popdown(overlay);
}

static void
overlay_window_on_menu_skip_clicked(GtkButton *button, gpointer user_data)
{
  (void)button;
  OverlayWindow *overlay = user_data;
  if (overlay == NULL || overlay->state == NULL || overlay->state->timer == NULL) {
    return;
  }

  pomodoro_timer_skip(overlay->state->timer);
  overlay_window_menu_popdown(overlay);
}

static void
overlay_window_on_menu_stop_clicked(GtkButton *button, gpointer user_data)
{
  (void)button;
  OverlayWindow *overlay = user_data;
  if (overlay == NULL || overlay->state == NULL || overlay->state->timer == NULL) {
    return;
  }

  pomodoro_timer_stop(overlay->state->timer);
  overlay_window_menu_popdown(overlay);
}

static void
overlay_window_on_menu_hide_clicked(GtkButton *button, gpointer user_data)
{
  (void)button;
  OverlayWindow *overlay = user_data;
  if (overlay == NULL || overlay->state == NULL) {
    return;
  }

  overlay_window_menu_popdown(overlay);
  overlay_window_set_visible(overlay->state, FALSE);
}

static void
overlay_window_on_menu_show_clicked(GtkButton *button, gpointer user_data)
{
  (void)button;
  OverlayWindow *overlay = user_data;
  if (overlay == NULL || overlay->state == NULL || overlay->state->window == NULL) {
    return;
  }

  gtk_window_present(overlay->state->window);
  overlay_window_menu_popdown(overlay);
}

static void
overlay_window_on_menu_quit_clicked(GtkButton *button, gpointer user_data)
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

void
overlay_window_bind_actions(OverlayWindow *overlay, GtkWidget *bubble)
{
  if (overlay == NULL) {
    return;
  }

  if (overlay->opacity_scale != NULL) {
    g_signal_connect(overlay->opacity_scale,
                     "value-changed",
                     G_CALLBACK(overlay_window_on_opacity_changed),
                     overlay);
  }

  if (overlay->menu_popover != NULL) {
    g_signal_connect(overlay->menu_popover,
                     "closed",
                     G_CALLBACK(overlay_window_on_menu_popover_closed),
                     overlay);
  }

  if (overlay->menu_toggle_button != NULL) {
    g_signal_connect(overlay->menu_toggle_button,
                     "clicked",
                     G_CALLBACK(overlay_window_on_menu_toggle_clicked),
                     overlay);
  }

  if (overlay->menu_skip_button != NULL) {
    g_signal_connect(overlay->menu_skip_button,
                     "clicked",
                     G_CALLBACK(overlay_window_on_menu_skip_clicked),
                     overlay);
  }

  if (overlay->menu_stop_button != NULL) {
    g_signal_connect(overlay->menu_stop_button,
                     "clicked",
                     G_CALLBACK(overlay_window_on_menu_stop_clicked),
                     overlay);
  }

  if (overlay->menu_hide_button != NULL) {
    g_signal_connect(overlay->menu_hide_button,
                     "clicked",
                     G_CALLBACK(overlay_window_on_menu_hide_clicked),
                     overlay);
  }

  if (overlay->menu_show_button != NULL) {
    g_signal_connect(overlay->menu_show_button,
                     "clicked",
                     G_CALLBACK(overlay_window_on_menu_show_clicked),
                     overlay);
  }

  if (overlay->menu_quit_button != NULL) {
    g_signal_connect(overlay->menu_quit_button,
                     "clicked",
                     G_CALLBACK(overlay_window_on_menu_quit_clicked),
                     overlay);
  }

  if (overlay->root != NULL) {
    GtkEventController *motion = gtk_event_controller_motion_new();
    g_signal_connect(motion,
                     "enter",
                     G_CALLBACK(overlay_window_on_overlay_pointer_enter),
                     overlay);
    g_signal_connect(motion,
                     "leave",
                     G_CALLBACK(overlay_window_on_overlay_pointer_leave),
                     overlay);
    gtk_widget_add_controller(overlay->root, motion);

    GtkGesture *right_click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(right_click),
                                  GDK_BUTTON_SECONDARY);
    g_signal_connect(right_click,
                     "pressed",
                     G_CALLBACK(overlay_window_on_overlay_right_click),
                     overlay);
    gtk_widget_add_controller(overlay->root, GTK_EVENT_CONTROLLER(right_click));
  }

  if (bubble != NULL) {
    GtkGesture *drag = gtk_gesture_drag_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(drag), GDK_BUTTON_PRIMARY);
    g_signal_connect(drag,
                     "drag-begin",
                     G_CALLBACK(overlay_window_on_overlay_drag_begin),
                     overlay);
    gtk_widget_add_controller(bubble, GTK_EVENT_CONTROLLER(drag));
  }
}
