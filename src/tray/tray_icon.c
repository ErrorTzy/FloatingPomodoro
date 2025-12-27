#include "tray/tray_item_internal.h"

#include <cairo.h>

GVariant *
tray_icon_pixmap_empty(void)
{
  GVariantBuilder builder;
  g_variant_builder_init(&builder, G_VARIANT_TYPE("a(iiay)"));
  return g_variant_builder_end(&builder);
}

GVariant *
tray_icon_pixmap_draw(int size)
{
  const double teal_r = 0x0F / 255.0;
  const double teal_g = 0x4C / 255.0;
  const double teal_b = 0x5C / 255.0;
  const double ivory_r = 0xF6 / 255.0;
  const double ivory_g = 0xF1 / 255.0;
  const double ivory_b = 0xE7 / 255.0;
  const double orange_r = 0xE3 / 255.0;
  const double orange_g = 0x64 / 255.0;
  const double orange_b = 0x14 / 255.0;

  cairo_surface_t *surface =
      cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
  cairo_t *cr = cairo_create(surface);

  cairo_set_source_rgba(cr, 0, 0, 0, 0);
  cairo_paint(cr);

  double cx = size / 2.0;
  double cy = size / 2.0;
  double margin = MAX(1.0, size * 0.0625);
  double outer_radius = MAX(1.0, (size / 2.0) - margin);
  double inner_radius = outer_radius * (86.0 / 112.0);
  double ring_width = MAX(1.0, size * 0.0625);
  ring_width = MIN(ring_width, inner_radius * 0.6);
  double dot_radius = MAX(1.0, size * (10.0 / 256.0));

  cairo_set_source_rgba(cr, teal_r, teal_g, teal_b, 1.0);
  cairo_arc(cr, cx, cy, outer_radius, 0, 2 * G_PI);
  cairo_fill(cr);

  cairo_set_source_rgba(cr, ivory_r, ivory_g, ivory_b, 1.0);
  cairo_arc(cr, cx, cy, inner_radius, 0, 2 * G_PI);
  cairo_fill(cr);

  if (ring_width > 0.5) {
    double circumference = 2.0 * G_PI * inner_radius;
    double dash_on = circumference * (260.0 / 540.0);
    double dash_off = MAX(0.0, circumference - dash_on);
    double dashes[] = {dash_on, dash_off};

    cairo_set_line_width(cr, ring_width);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_source_rgba(cr, orange_r, orange_g, orange_b, 1.0);
    cairo_set_dash(cr, dashes, 2, 0.0);

    cairo_save(cr);
    cairo_translate(cr, cx, cy);
    cairo_rotate(cr, -G_PI_2);
    cairo_translate(cr, -cx, -cy);
    cairo_arc(cr, cx, cy, inner_radius, 0, 2 * G_PI);
    cairo_stroke(cr);
    cairo_restore(cr);

    cairo_set_dash(cr, NULL, 0, 0.0);
  }

  cairo_set_source_rgba(cr, teal_r, teal_g, teal_b, 1.0);
  cairo_arc(cr, cx, cy, dot_radius, 0, 2 * G_PI);
  cairo_fill(cr);

  cairo_destroy(cr);
  cairo_surface_flush(surface);

  int stride = cairo_image_surface_get_stride(surface);
  gsize len = (gsize)stride * (gsize)size;
  const guint8 *data = cairo_image_surface_get_data(surface);
  guint8 *copy = g_memdup2(data, len);
  cairo_surface_destroy(surface);

  GVariantBuilder bytes;
  g_variant_builder_init(&bytes, G_VARIANT_TYPE("ay"));
  for (gsize i = 0; i < len; i++) {
    g_variant_builder_add(&bytes, "y", copy[i]);
  }
  g_free(copy);

  GVariant *bytes_variant = g_variant_builder_end(&bytes);
  GVariantBuilder pixmap;
  g_variant_builder_init(&pixmap, G_VARIANT_TYPE("a(iiay)"));
  g_variant_builder_add(&pixmap, "(ii@ay)", size, size, bytes_variant);
  return g_variant_builder_end(&pixmap);
}
