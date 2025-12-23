#include <gtk/gtk.h>

#include "config.h"

typedef enum {
  LOG_LEVEL_WARN = 0,
  LOG_LEVEL_INFO = 1,
  LOG_LEVEL_DEBUG = 2
} LogLevel;

static LogLevel log_level_threshold = LOG_LEVEL_WARN;

static LogLevel
parse_log_level(void)
{
  const char *value = g_getenv("POMODORO_LOG_LEVEL");

  if (value == NULL || *value == '\0') {
    return LOG_LEVEL_WARN;
  }

  if (g_ascii_strcasecmp(value, "debug") == 0) {
    return LOG_LEVEL_DEBUG;
  }

  if (g_ascii_strcasecmp(value, "info") == 0 ||
      g_ascii_strcasecmp(value, "message") == 0) {
    return LOG_LEVEL_INFO;
  }

  if (g_ascii_strcasecmp(value, "warn") == 0 ||
      g_ascii_strcasecmp(value, "warning") == 0 ||
      g_ascii_strcasecmp(value, "error") == 0) {
    return LOG_LEVEL_WARN;
  }

  g_warning("Unknown POMODORO_LOG_LEVEL='%s', defaulting to 'warn'", value);
  return LOG_LEVEL_WARN;
}

static gboolean
should_log(GLogLevelFlags level)
{
  if (level & (G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING)) {
    return TRUE;
  }

  if ((level & (G_LOG_LEVEL_MESSAGE | G_LOG_LEVEL_INFO)) != 0) {
    return log_level_threshold >= LOG_LEVEL_INFO;
  }

  if ((level & G_LOG_LEVEL_DEBUG) != 0) {
    return log_level_threshold >= LOG_LEVEL_DEBUG;
  }

  return FALSE;
}

static GLogWriterOutput
pomodoro_log_writer(GLogLevelFlags level,
                    const GLogField *fields,
                    gsize n_fields,
                    gpointer user_data)
{
  (void)user_data;

  if (!should_log(level)) {
    return G_LOG_WRITER_HANDLED;
  }

  return g_log_writer_default(level, fields, n_fields, user_data);
}

static void
install_logging(void)
{
  log_level_threshold = parse_log_level();
  g_log_set_writer_func(pomodoro_log_writer, NULL, NULL);
}

static void
on_activate(GtkApplication *app, gpointer user_data)
{
  (void)user_data;

  GtkWidget *window = gtk_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(window), APP_NAME);
  gtk_window_set_default_size(GTK_WINDOW(window), 880, 560);

  GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_top(content, 24);
  gtk_widget_set_margin_bottom(content, 24);
  gtk_widget_set_margin_start(content, 28);
  gtk_widget_set_margin_end(content, 28);

  GtkWidget *title = gtk_label_new(APP_NAME);
  gtk_widget_add_css_class(title, "title-1");
  gtk_widget_set_halign(title, GTK_ALIGN_START);

  GtkWidget *subtitle = gtk_label_new("Project scaffolding is ready. Future screens will land here.");
  gtk_widget_set_halign(subtitle, GTK_ALIGN_START);
  gtk_label_set_wrap(GTK_LABEL(subtitle), TRUE);

  gtk_box_append(GTK_BOX(content), title);
  gtk_box_append(GTK_BOX(content), subtitle);

  gtk_window_set_child(GTK_WINDOW(window), content);
  gtk_window_present(GTK_WINDOW(window));

  g_info("Main window presented");
}

int
main(int argc, char *argv[])
{
  install_logging();

  g_set_application_name(APP_NAME);

  GtkApplication *app = gtk_application_new(APP_ID, G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);

  g_info("Starting %s", APP_NAME);

  int status = g_application_run(G_APPLICATION(app), argc, argv);

  g_object_unref(app);

  return status;
}
