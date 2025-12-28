#include "overlay/overlay_window_internal.h"

void overlay_window_update_warning_app_width(OverlayWindow *overlay,
                                             int bubble_width);
void overlay_window_update_warning_focus_size(OverlayWindow *overlay,
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

void
overlay_window_update_warning_app_width(OverlayWindow *overlay, int bubble_width)
{
  if (overlay == NULL || overlay->warning_app_label == NULL) {
    return;
  }

  if (bubble_width <= 0) {
    bubble_width = OVERLAY_BUBBLE_SIZE;
  }

  int max_width =
      (int)((double)bubble_width * OVERLAY_WARNING_APP_WIDTH_RATIO + 0.5);
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

void
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

  const char *text =
      gtk_label_get_text(GTK_LABEL(overlay->warning_focus_label));
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
