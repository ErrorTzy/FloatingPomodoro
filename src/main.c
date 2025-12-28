#include <gtk/gtk.h>

#include "app/app_init.h"
#include "config.h"
#include "ui/main_window.h"

static gboolean
extract_autostart_flag(int *argc, char ***argv)
{
  if (argc == NULL || argv == NULL || *argv == NULL) {
    return FALSE;
  }

  gboolean autostart = FALSE;
  int write_index = 0;
  for (int i = 0; i < *argc; i++) {
    if (g_strcmp0((*argv)[i], "--autostart") == 0) {
      autostart = TRUE;
      continue;
    }
    (*argv)[write_index++] = (*argv)[i];
  }
  (*argv)[write_index] = NULL;
  *argc = write_index;
  return autostart;
}

static void
on_startup(GApplication *app, gpointer user_data)
{
  (void)app;
  (void)user_data;

  app_init_fonts();
  app_init_icons();
  app_load_css();
}

static void
on_activate(GtkApplication *app, gpointer user_data)
{
  (void)user_data;
  gboolean autostart_launch =
      GPOINTER_TO_INT(g_object_get_data(G_OBJECT(app), "autostart-launch"));
  main_window_present(app, autostart_launch);
}

int
main(int argc, char *argv[])
{
  app_init_logging();
  app_init_crash_handler();
  app_register_resources();

  g_set_application_name(APP_NAME);

  gboolean autostart_launch = extract_autostart_flag(&argc, &argv);

  GtkApplication *app = gtk_application_new(APP_ID, G_APPLICATION_DEFAULT_FLAGS);
  g_object_set_data(G_OBJECT(app),
                    "autostart-launch",
                    GINT_TO_POINTER(autostart_launch));
  g_signal_connect(app, "startup", G_CALLBACK(on_startup), NULL);
  g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);

  g_info("Starting %s", APP_NAME);

  int status = g_application_run(G_APPLICATION(app), argc, argv);

  g_object_unref(app);

  return status;
}
