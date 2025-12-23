#include <gtk/gtk.h>

#include "config.h"
#include "xfce4_floating_pomodoro_resources.h"

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
install_resources(void)
{
  g_resources_register(xfce4_floating_pomodoro_get_resource());
}

static void
load_css(void)
{
  GtkCssProvider *provider = gtk_css_provider_new();

  gtk_css_provider_load_from_resource(
      provider,
      "/com/scott/Xfce4FloatingPomodoro/styles/app.css");
  gtk_style_context_add_provider_for_display(
      gdk_display_get_default(),
      GTK_STYLE_PROVIDER(provider),
      GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  g_object_unref(provider);
}

static void
on_startup(GApplication *app, gpointer user_data)
{
  (void)app;
  (void)user_data;

  load_css();
}

static void
on_activate(GtkApplication *app, gpointer user_data)
{
  (void)user_data;

  GtkWidget *window = gtk_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(window), APP_NAME);
  gtk_window_set_default_size(GTK_WINDOW(window), 880, 560);
  gtk_widget_add_css_class(window, "app-window");

  GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
  gtk_widget_add_css_class(root, "app-root");

  GtkWidget *header = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
  gtk_widget_add_css_class(header, "app-header");

  GtkWidget *title = gtk_label_new(APP_NAME);
  gtk_widget_add_css_class(title, "app-title");
  gtk_widget_set_halign(title, GTK_ALIGN_START);

  GtkWidget *subtitle = gtk_label_new("Design system is live. Task flow and timer controls arrive next.");
  gtk_widget_add_css_class(subtitle, "app-subtitle");
  gtk_widget_set_halign(subtitle, GTK_ALIGN_START);
  gtk_label_set_wrap(GTK_LABEL(subtitle), TRUE);

  gtk_box_append(GTK_BOX(header), title);
  gtk_box_append(GTK_BOX(header), subtitle);
  gtk_box_append(GTK_BOX(root), header);

  GtkWidget *hero = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 18);
  gtk_widget_set_hexpand(hero, TRUE);

  GtkWidget *timer_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_add_css_class(timer_card, "card");
  gtk_widget_set_hexpand(timer_card, TRUE);

  GtkWidget *timer_title = gtk_label_new("Focus Session");
  gtk_widget_add_css_class(timer_title, "card-title");
  gtk_widget_set_halign(timer_title, GTK_ALIGN_START);

  GtkWidget *timer_value = gtk_label_new("25:00");
  gtk_widget_add_css_class(timer_value, "timer-value");
  gtk_widget_set_halign(timer_value, GTK_ALIGN_START);

  GtkWidget *timer_pill = gtk_label_new("Next: short break");
  gtk_widget_add_css_class(timer_pill, "pill");
  gtk_widget_set_halign(timer_pill, GTK_ALIGN_START);

  GtkWidget *timer_actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  gtk_widget_set_halign(timer_actions, GTK_ALIGN_START);

  GtkWidget *start_button = gtk_button_new_with_label("Start Focus");
  gtk_widget_add_css_class(start_button, "btn-primary");

  GtkWidget *skip_button = gtk_button_new_with_label("Skip");
  gtk_widget_add_css_class(skip_button, "btn-secondary");

  gtk_box_append(GTK_BOX(timer_actions), start_button);
  gtk_box_append(GTK_BOX(timer_actions), skip_button);

  gtk_box_append(GTK_BOX(timer_card), timer_title);
  gtk_box_append(GTK_BOX(timer_card), timer_value);
  gtk_box_append(GTK_BOX(timer_card), timer_pill);
  gtk_box_append(GTK_BOX(timer_card), timer_actions);

  GtkWidget *task_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_widget_add_css_class(task_card, "card");
  gtk_widget_set_hexpand(task_card, TRUE);

  GtkWidget *task_title = gtk_label_new("Current Task");
  gtk_widget_add_css_class(task_title, "card-title");
  gtk_widget_set_halign(task_title, GTK_ALIGN_START);

  GtkWidget *task_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_halign(task_row, GTK_ALIGN_START);

  GtkWidget *task_label = gtk_label_new("Design focus flow");
  gtk_widget_add_css_class(task_label, "task-item");

  GtkWidget *task_tag = gtk_label_new("In progress");
  gtk_widget_add_css_class(task_tag, "tag");

  gtk_box_append(GTK_BOX(task_row), task_label);
  gtk_box_append(GTK_BOX(task_row), task_tag);

  GtkWidget *task_meta = gtk_label_new("2 cycles completed today");
  gtk_widget_add_css_class(task_meta, "task-meta");
  gtk_widget_set_halign(task_meta, GTK_ALIGN_START);

  GtkWidget *stats_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 18);
  gtk_widget_set_halign(stats_row, GTK_ALIGN_START);

  GtkWidget *stat_block_left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  GtkWidget *stat_value_left = gtk_label_new("03:40");
  gtk_widget_add_css_class(stat_value_left, "stat-value");
  GtkWidget *stat_label_left = gtk_label_new("Focus time");
  gtk_widget_add_css_class(stat_label_left, "stat-label");
  gtk_box_append(GTK_BOX(stat_block_left), stat_value_left);
  gtk_box_append(GTK_BOX(stat_block_left), stat_label_left);

  GtkWidget *stat_block_right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  GtkWidget *stat_value_right = gtk_label_new("1");
  gtk_widget_add_css_class(stat_value_right, "stat-value");
  GtkWidget *stat_label_right = gtk_label_new("Breaks");
  gtk_widget_add_css_class(stat_label_right, "stat-label");
  gtk_box_append(GTK_BOX(stat_block_right), stat_value_right);
  gtk_box_append(GTK_BOX(stat_block_right), stat_label_right);

  gtk_box_append(GTK_BOX(stats_row), stat_block_left);
  gtk_box_append(GTK_BOX(stats_row), stat_block_right);

  gtk_box_append(GTK_BOX(task_card), task_title);
  gtk_box_append(GTK_BOX(task_card), task_row);
  gtk_box_append(GTK_BOX(task_card), task_meta);
  gtk_box_append(GTK_BOX(task_card), stats_row);

  gtk_box_append(GTK_BOX(hero), timer_card);
  gtk_box_append(GTK_BOX(hero), task_card);
  gtk_box_append(GTK_BOX(root), hero);

  gtk_window_set_child(GTK_WINDOW(window), root);
  gtk_window_present(GTK_WINDOW(window));

  g_info("Main window presented");
}

int
main(int argc, char *argv[])
{
  install_logging();
  install_resources();

  g_set_application_name(APP_NAME);

  GtkApplication *app = gtk_application_new(APP_ID, G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect(app, "startup", G_CALLBACK(on_startup), NULL);
  g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);

  g_info("Starting %s", APP_NAME);

  int status = g_application_run(G_APPLICATION(app), argc, argv);

  g_object_unref(app);

  return status;
}
