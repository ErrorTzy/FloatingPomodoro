#include "overlay/overlay_window_internal.h"

#define OVERLAY_BUBBLE_SIZE 128
#define OVERLAY_WINDOW_MARGIN 6
#define OVERLAY_WARNING_HALO_PADDING 32
#define OVERLAY_WARNING_APP_WIDTH_RATIO 0.6
#define OVERLAY_WARNING_FOCUS_WIDTH_RATIO 0.7
#define OVERLAY_SIZE_TICK_DEFAULT_MS 300

static void
overlay_window_update_warning_app_width(OverlayWindow *overlay,
                                        int bubble_width);
static void
overlay_window_update_warning_focus_size(OverlayWindow *overlay,
                                         int bubble_width);

static gboolean
overlay_window_size_tick(GtkWidget *widget,
                         GdkFrameClock *frame_clock,
                         gpointer user_data)
{
  (void)widget;
  (void)frame_clock;
  OverlayWindow *overlay = user_data;
  if (overlay == NULL) {
    return G_SOURCE_REMOVE;
  }

  int width = 0;
  int height = 0;
  if (overlay->bubble != NULL) {
    width = gtk_widget_get_width(overlay->bubble);
    height = gtk_widget_get_height(overlay->bubble);
  }

  int bubble_size = width < height ? width : height;
  overlay_window_update_warning_app_width(overlay, bubble_size);
  overlay_window_update_warning_focus_size(overlay, bubble_size);
  overlay_window_update_input_region(overlay);

  if (g_get_monotonic_time() >= overlay->size_tick_until_us) {
    overlay->size_tick_id = 0;
    return G_SOURCE_REMOVE;
  }

  return G_SOURCE_CONTINUE;
}

void
overlay_window_request_size_updates(OverlayWindow *overlay, guint duration_ms)
{
  if (overlay == NULL || overlay->root == NULL) {
    return;
  }

  if (duration_ms == 0) {
    duration_ms = OVERLAY_SIZE_TICK_DEFAULT_MS;
  }

  gint64 now = g_get_monotonic_time();
  gint64 until = now + ((gint64)duration_ms * 1000);
  if (until > overlay->size_tick_until_us) {
    overlay->size_tick_until_us = until;
  }

  if (overlay->size_tick_id == 0) {
    overlay->size_tick_id =
        gtk_widget_add_tick_callback(overlay->root,
                                     overlay_window_size_tick,
                                     overlay,
                                     NULL);
  }
}

static void
overlay_window_update_warning_app_width(OverlayWindow *overlay, int bubble_width)
{
  if (overlay == NULL || overlay->warning_app_label == NULL) {
    return;
  }

  if (bubble_width <= 0) {
    bubble_width = OVERLAY_BUBBLE_SIZE;
  }

  int max_width = (int)((double)bubble_width * OVERLAY_WARNING_APP_WIDTH_RATIO + 0.5);
  if (max_width < 12) {
    max_width = 12;
  }

  PangoContext *context =
      gtk_widget_get_pango_context(overlay->warning_app_label);
  if (context == NULL) {
    gtk_label_set_max_width_chars(GTK_LABEL(overlay->warning_app_label), 10);
    return;
  }

  PangoFontMetrics *metrics =
      pango_context_get_metrics(context,
                                pango_context_get_font_description(context),
                                pango_context_get_language(context));
  int char_width = 0;
  if (metrics != NULL) {
    char_width = pango_font_metrics_get_approximate_char_width(metrics) /
                 PANGO_SCALE;
    pango_font_metrics_unref(metrics);
  }

  if (char_width <= 0) {
    gtk_label_set_max_width_chars(GTK_LABEL(overlay->warning_app_label), 10);
    return;
  }

  int max_chars = max_width / char_width;
  if (max_chars < 1) {
    max_chars = 1;
  }

  gtk_label_set_max_width_chars(GTK_LABEL(overlay->warning_app_label), max_chars);
}

static void
overlay_window_update_warning_focus_size(OverlayWindow *overlay, int bubble_width)
{
  if (overlay == NULL || overlay->warning_focus_label == NULL) {
    return;
  }

  if (bubble_width <= 0) {
    bubble_width = OVERLAY_BUBBLE_SIZE;
  }

  int target_width =
      (int)((double)bubble_width * OVERLAY_WARNING_FOCUS_WIDTH_RATIO + 0.5);
  if (target_width < 1) {
    return;
  }

  PangoContext *context =
      gtk_widget_get_pango_context(overlay->warning_focus_label);
  if (context == NULL) {
    return;
  }

  const char *text = gtk_label_get_text(GTK_LABEL(overlay->warning_focus_label));
  if (text == NULL || *text == '\0') {
    text = "FOCUS!";
  }

  PangoFontDescription *desc =
      pango_font_description_copy(pango_context_get_font_description(context));
  if (desc == NULL) {
    return;
  }

  int base_size = pango_font_description_get_size(desc);
  if (base_size <= 0) {
    base_size = 12 * PANGO_SCALE;
    pango_font_description_set_size(desc, base_size);
  }

  PangoLayout *layout = pango_layout_new(context);
  pango_layout_set_text(layout, text, -1);
  pango_layout_set_font_description(layout, desc);

  int base_width = 0;
  pango_layout_get_pixel_size(layout, &base_width, NULL);
  g_object_unref(layout);
  pango_font_description_free(desc);

  if (base_width <= 0) {
    return;
  }

  double scale = (double)target_width / (double)base_width;
  if (scale < 1.0) {
    gtk_label_set_attributes(GTK_LABEL(overlay->warning_focus_label), NULL);
    return;
  }

  int new_size = (int)(base_size * scale);
  int max_size = 48 * PANGO_SCALE;
  if (new_size > max_size) {
    new_size = max_size;
  }

  PangoAttrList *attrs = pango_attr_list_new();
  PangoAttribute *size_attr = pango_attr_size_new(new_size);
  size_attr->start_index = 0;
  size_attr->end_index = G_MAXUINT;
  pango_attr_list_insert(attrs, size_attr);
  gtk_label_set_attributes(GTK_LABEL(overlay->warning_focus_label), attrs);
  pango_attr_list_unref(attrs);
}

static GtkWidget *
overlay_window_create_menu_icon_button(const char *icon_name,
                                       const char *label,
                                       GtkWidget **icon_out)
{
  GtkWidget *button = gtk_button_new();
  gtk_widget_add_css_class(button, "icon-button");
  gtk_widget_add_css_class(button, "overlay-menu-icon");
  gtk_widget_set_size_request(button, 30, 30);

  GtkWidget *icon = gtk_image_new_from_icon_name(icon_name);
  gtk_image_set_pixel_size(GTK_IMAGE(icon), 16);
  gtk_button_set_child(GTK_BUTTON(button), icon);

  if (icon_out != NULL) {
    *icon_out = icon;
  }

  if (label != NULL) {
    gtk_widget_set_tooltip_text(button, label);
    gtk_accessible_update_property(GTK_ACCESSIBLE(button),
                                   GTK_ACCESSIBLE_PROPERTY_LABEL,
                                   label,
                                   -1);
  }

  return button;
}

static GtkWidget *
overlay_window_create_menu_text_button(const char *label,
                                       const char *css_class)
{
  GtkWidget *button = gtk_button_new_with_label(label);
  gtk_widget_add_css_class(button, "overlay-menu-text");
  if (css_class != NULL) {
    gtk_widget_add_css_class(button, css_class);
  }
  gtk_widget_set_halign(button, GTK_ALIGN_FILL);
  gtk_widget_set_hexpand(button, TRUE);
  if (label != NULL) {
    gtk_accessible_update_property(GTK_ACCESSIBLE(button),
                                   GTK_ACCESSIBLE_PROPERTY_LABEL,
                                   label,
                                   -1);
  }
  return button;
}

GtkWindow *
overlay_window_create_window(GtkApplication *app)
{
  GtkWidget *window = gtk_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(window), "Pomodoro Overlay");
  gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
  gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
  int window_size = OVERLAY_BUBBLE_SIZE + (OVERLAY_WINDOW_MARGIN * 2) +
                    (OVERLAY_WARNING_HALO_PADDING * 2);
  gtk_window_set_default_size(GTK_WINDOW(window), window_size, window_size);
  gtk_window_set_focus_visible(GTK_WINDOW(window), FALSE);
  gtk_window_set_deletable(GTK_WINDOW(window), FALSE);
  gtk_widget_add_css_class(window, "overlay-window");
  return GTK_WINDOW(window);
}

void
overlay_window_build_ui(OverlayWindow *overlay)
{
  if (overlay == NULL || overlay->window == NULL) {
    return;
  }

  GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_widget_add_css_class(root, "overlay-root");
  gtk_widget_set_margin_top(root, OVERLAY_WINDOW_MARGIN);
  gtk_widget_set_margin_bottom(root, OVERLAY_WINDOW_MARGIN);
  gtk_widget_set_margin_start(root, OVERLAY_WINDOW_MARGIN);
  gtk_widget_set_margin_end(root, OVERLAY_WINDOW_MARGIN);
  overlay->root = root;
  overlay_window_set_opacity(overlay, overlay->opacity);

  GtkWidget *bubble = gtk_overlay_new();
  gtk_widget_add_css_class(bubble, "overlay-bubble");
  gtk_widget_set_size_request(bubble, OVERLAY_BUBBLE_SIZE, OVERLAY_BUBBLE_SIZE);
  gtk_widget_set_hexpand(bubble, TRUE);
  gtk_widget_set_vexpand(bubble, TRUE);
  gtk_widget_set_overflow(bubble, GTK_OVERFLOW_VISIBLE);
  overlay->bubble = bubble;

  GtkWidget *bubble_frame =
      gtk_aspect_frame_new(0.5f, 0.0f, 1.0f, FALSE);
  gtk_widget_set_halign(bubble_frame, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(bubble_frame, GTK_ALIGN_START);
  gtk_widget_set_hexpand(bubble_frame, TRUE);
  gtk_widget_set_vexpand(bubble_frame, FALSE);
  gtk_widget_set_margin_top(bubble_frame, OVERLAY_WARNING_HALO_PADDING);
  gtk_widget_set_margin_bottom(bubble_frame, OVERLAY_WARNING_HALO_PADDING);
  gtk_widget_set_margin_start(bubble_frame, OVERLAY_WARNING_HALO_PADDING);
  gtk_widget_set_margin_end(bubble_frame, OVERLAY_WARNING_HALO_PADDING);
  gtk_aspect_frame_set_child(GTK_ASPECT_FRAME(bubble_frame), bubble);
  overlay->bubble_frame = bubble_frame;

  GtkWidget *drawing = gtk_drawing_area_new();
  gtk_widget_set_hexpand(drawing, TRUE);
  gtk_widget_set_vexpand(drawing, TRUE);
  gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(drawing),
                                     OVERLAY_BUBBLE_SIZE);
  gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(drawing),
                                      OVERLAY_BUBBLE_SIZE);
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(drawing),
                                 overlay_window_draw,
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

  GtkWidget *warning_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
  gtk_widget_add_css_class(warning_box, "overlay-warning-text");
  gtk_widget_set_halign(warning_box, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(warning_box, GTK_ALIGN_CENTER);
  gtk_widget_set_visible(warning_box, FALSE);
  overlay->warning_box = warning_box;

  GtkWidget *warning_title = gtk_label_new("STAY");
  gtk_widget_add_css_class(warning_title, "overlay-warning-title");
  gtk_widget_set_halign(warning_title, GTK_ALIGN_CENTER);
  gtk_label_set_xalign(GTK_LABEL(warning_title), 0.5f);
  overlay->warning_title_label = warning_title;

  GtkWidget *warning_focus = gtk_label_new("FOCUS!");
  gtk_widget_add_css_class(warning_focus, "overlay-warning-focus");
  gtk_widget_set_halign(warning_focus, GTK_ALIGN_CENTER);
  gtk_label_set_xalign(GTK_LABEL(warning_focus), 0.5f);
  gtk_label_set_single_line_mode(GTK_LABEL(warning_focus), TRUE);
  overlay->warning_focus_label = warning_focus;

  GtkWidget *warning_app = gtk_label_new("");
  gtk_widget_add_css_class(warning_app, "overlay-warning-app");
  gtk_widget_set_halign(warning_app, GTK_ALIGN_CENTER);
  gtk_label_set_xalign(GTK_LABEL(warning_app), 0.5f);
  gtk_label_set_single_line_mode(GTK_LABEL(warning_app), TRUE);
  gtk_label_set_ellipsize(GTK_LABEL(warning_app), PANGO_ELLIPSIZE_END);
  gtk_widget_set_hexpand(warning_app, FALSE);
  overlay->warning_app_label = warning_app;

  gtk_box_append(GTK_BOX(warning_box), warning_title);
  gtk_box_append(GTK_BOX(warning_box), warning_focus);
  gtk_box_append(GTK_BOX(warning_box), warning_app);

  gtk_box_append(GTK_BOX(label_box), time_label);
  gtk_box_append(GTK_BOX(label_box), phase_label);
  gtk_box_append(GTK_BOX(label_box), warning_box);
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
  overlay->opacity_scale = opacity_scale;

  gtk_box_append(GTK_BOX(opacity_row), opacity_label);
  gtk_box_append(GTK_BOX(opacity_row), opacity_scale);

  gtk_box_append(GTK_BOX(panel), current_title);
  gtk_box_append(GTK_BOX(panel), current_value);
  gtk_box_append(GTK_BOX(panel), next_title);
  gtk_box_append(GTK_BOX(panel), next_value);
  gtk_box_append(GTK_BOX(panel), opacity_row);

  gtk_revealer_set_child(GTK_REVEALER(revealer), panel);

  GtkWidget *menu_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
  gtk_widget_add_css_class(menu_box, "overlay-menu");

  GtkWidget *action_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
  gtk_widget_add_css_class(action_row, "overlay-menu-actions");
  gtk_widget_set_halign(action_row, GTK_ALIGN_CENTER);

  GtkWidget *toggle_button = overlay_window_create_menu_icon_button(
      "media-playback-start-symbolic",
      "Start Focus",
      &overlay->menu_toggle_icon);
  overlay->menu_toggle_button = toggle_button;

  GtkWidget *skip_button =
      overlay_window_create_menu_icon_button("media-skip-forward-symbolic",
                                             "Skip",
                                             NULL);
  overlay->menu_skip_button = skip_button;

  GtkWidget *stop_button =
      overlay_window_create_menu_icon_button("media-playback-stop-symbolic",
                                             "Stop",
                                             NULL);
  gtk_widget_add_css_class(stop_button, "icon-danger");
  overlay->menu_stop_button = stop_button;

  GtkWidget *actions_divider = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_add_css_class(actions_divider, "overlay-menu-divider");

  GtkWidget *text_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_add_css_class(text_box, "overlay-menu-links");
  gtk_widget_set_halign(text_box, GTK_ALIGN_FILL);
  gtk_widget_set_hexpand(text_box, TRUE);

  GtkWidget *hide_button = overlay_window_create_menu_text_button("Hide", NULL);
  overlay->menu_hide_button = hide_button;

  GtkWidget *text_divider_one = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_add_css_class(text_divider_one, "overlay-menu-divider");

  GtkWidget *show_button =
      overlay_window_create_menu_text_button("Open App", NULL);
  overlay->menu_show_button = show_button;

  GtkWidget *text_divider_two = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_add_css_class(text_divider_two, "overlay-menu-divider");

  GtkWidget *quit_button =
      overlay_window_create_menu_text_button("Quit", "overlay-menu-danger");
  overlay->menu_quit_button = quit_button;

  gtk_box_append(GTK_BOX(action_row), toggle_button);
  gtk_box_append(GTK_BOX(action_row), skip_button);
  gtk_box_append(GTK_BOX(action_row), stop_button);

  gtk_box_append(GTK_BOX(text_box), hide_button);
  gtk_box_append(GTK_BOX(text_box), text_divider_one);
  gtk_box_append(GTK_BOX(text_box), show_button);
  gtk_box_append(GTK_BOX(text_box), text_divider_two);
  gtk_box_append(GTK_BOX(text_box), quit_button);

  gtk_box_append(GTK_BOX(menu_box), action_row);
  gtk_box_append(GTK_BOX(menu_box), actions_divider);
  gtk_box_append(GTK_BOX(menu_box), text_box);

  GtkWidget *popover = gtk_popover_new();
  gtk_popover_set_has_arrow(GTK_POPOVER(popover), FALSE);
  gtk_popover_set_autohide(GTK_POPOVER(popover), TRUE);
  gtk_popover_set_child(GTK_POPOVER(popover), menu_box);
  gtk_widget_add_css_class(popover, "overlay-menu-popover");
  gtk_widget_set_parent(popover, root);
  overlay->menu_popover = popover;

  gtk_box_append(GTK_BOX(root), bubble_frame);
  gtk_box_append(GTK_BOX(root), revealer);

  gtk_window_set_child(GTK_WINDOW(overlay->window), root);

  overlay_window_update_warning_app_width(overlay, OVERLAY_BUBBLE_SIZE);
  overlay_window_update_warning_focus_size(overlay, OVERLAY_BUBBLE_SIZE);

  overlay_window_bind_actions(overlay, bubble);
  overlay_window_request_size_updates(overlay, OVERLAY_SIZE_TICK_DEFAULT_MS);
}
