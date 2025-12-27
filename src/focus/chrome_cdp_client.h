#pragma once

#include <gio/gio.h>
#include <glib.h>

typedef struct {
  char *title;
  char *url;
  char *text;
} ChromeCdpPage;

ChromeCdpPage *chrome_cdp_fetch_page_sync(guint port,
                                          const char *window_title,
                                          GCancellable *cancellable,
                                          GError **error);
void chrome_cdp_page_free(ChromeCdpPage *page);
