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
  cairo_surface_t *surface =
      cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
  cairo_t *cr = cairo_create(surface);

  cairo_set_source_rgba(cr, 0, 0, 0, 0);
  cairo_paint(cr);

  double radius = (size / 2.0) - 2.0;
  double cx = size / 2.0;
  double cy = size / 2.0;

  cairo_set_line_width(cr, MAX(2.0, size * 0.14));
  cairo_set_source_rgba(cr, 0.06, 0.30, 0.36, 0.95);
  cairo_arc(cr, cx, cy, radius, 0, 2 * G_PI);
  cairo_stroke(cr);

  cairo_set_source_rgba(cr, 0.96, 0.92, 0.87, 0.95);
  cairo_arc(cr, cx, cy, radius * 0.38, 0, 2 * G_PI);
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
