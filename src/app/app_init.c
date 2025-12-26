#include <execinfo.h>
#include <fontconfig/fontconfig.h>
#include <gtk/gtk.h>
#include <pango/pangocairo.h>
#include <signal.h>
#include <unistd.h>

#include "app/app_init.h"
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

void
app_init_logging(void)
{
  log_level_threshold = parse_log_level();
  if (log_level_threshold >= LOG_LEVEL_INFO &&
      g_getenv("G_MESSAGES_DEBUG") == NULL) {
    g_setenv("G_MESSAGES_DEBUG", "all", FALSE);
  }
  g_log_set_writer_func(pomodoro_log_writer, NULL, NULL);
}

static void
crash_handler(int signum)
{
  void *frames[48];
  int size = backtrace(frames, (int)(sizeof(frames) / sizeof(frames[0])));
  g_printerr("Fatal signal %d received\n", signum);
  backtrace_symbols_fd(frames, size, STDERR_FILENO);
  _exit(1);
}

void
app_init_crash_handler(void)
{
  signal(SIGSEGV, crash_handler);
  signal(SIGABRT, crash_handler);
}

void
app_register_resources(void)
{
  g_resources_register(xfce4_floating_pomodoro_get_resource());
}

void
app_init_fonts(void)
{
  const char *font_resources[] = {
      "/com/scott/Xfce4FloatingPomodoro/fonts/Manrope-Regular.ttf",
      "/com/scott/Xfce4FloatingPomodoro/fonts/Manrope-SemiBold.ttf",
      "/com/scott/Xfce4FloatingPomodoro/fonts/Manrope-Bold.ttf"};
  const char *font_files[] = {"Manrope-Regular.ttf",
                              "Manrope-SemiBold.ttf",
                              "Manrope-Bold.ttf"};
  const size_t font_count = G_N_ELEMENTS(font_resources);

  char *font_dir = g_build_filename(g_get_user_cache_dir(),
                                    "xfce4-floating-pomodoro",
                                    "fonts",
                                    NULL);
  g_mkdir_with_parents(font_dir, 0755);

  for (size_t i = 0; i < font_count; i++) {
    char *target_path = g_build_filename(font_dir, font_files[i], NULL);
    if (!g_file_test(target_path, G_FILE_TEST_EXISTS)) {
      GError *error = NULL;
      GBytes *data = g_resources_lookup_data(font_resources[i],
                                             G_RESOURCE_LOOKUP_FLAGS_NONE,
                                             &error);
      if (data == NULL) {
        g_warning("Failed to load font resource '%s': %s",
                  font_resources[i],
                  error ? error->message : "unknown error");
        g_clear_error(&error);
      } else {
        gsize length = 0;
        const char *bytes = g_bytes_get_data(data, &length);
        if (!g_file_set_contents(target_path, bytes, length, &error)) {
          g_warning("Failed to write font file '%s': %s",
                    target_path,
                    error ? error->message : "unknown error");
          g_clear_error(&error);
        }
        g_bytes_unref(data);
      }
    }
    g_free(target_path);
  }

  FcConfig *config = FcInitLoadConfigAndFonts();
  if (config == NULL) {
    g_debug("Fontconfig not available; skipping bundled font registration");
    g_free(font_dir);
    return;
  }

  FcConfigSetCurrent(config);

  gboolean added = FcConfigAppFontAddDir(config,
                                         (const FcChar8 *)font_dir);
  if (!added) {
    g_debug("Failed to register bundled fonts from '%s'", font_dir);
    g_free(font_dir);
    return;
  }

  PangoFontMap *fontmap = pango_cairo_font_map_get_default();
  if (fontmap != NULL) {
    pango_font_map_changed(fontmap);
  }

  g_free(font_dir);
}

void
app_load_css(void)
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
