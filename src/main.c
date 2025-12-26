#include <gtk/gtk.h>

#include "app/app_init.h"
#include "config.h"
#include "ui/main_window.h"

static void
on_startup(GApplication *app, gpointer user_data)
{
  (void)app;
  (void)user_data;

  app_init_fonts();
  app_load_css();
}

static void
on_activate(GtkApplication *app, gpointer user_data)
{
  (void)user_data;
  main_window_present(app);
}

int
main(int argc, char *argv[])
{
  app_init_logging();
  app_init_crash_handler();
  app_register_resources();

  g_set_application_name(APP_NAME);

  GtkApplication *app = gtk_application_new(APP_ID, G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect(app, "startup", G_CALLBACK(on_startup), NULL);
  g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);

  g_info("Starting %s", APP_NAME);

  int status = g_application_run(G_APPLICATION(app), argc, argv);

  g_object_unref(app);

  return status;
}
