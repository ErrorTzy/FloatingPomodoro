#include "utils/autostart.h"

#include <errno.h>
#include <glib/gstdio.h>

#include "config.h"

static char *
autostart_get_path(void)
{
  return g_build_filename(g_get_user_config_dir(),
                          "autostart",
                          "xfce4-floating-pomodoro.desktop",
                          NULL);
}

static gboolean
autostart_write_file(const char *path, GError **error)
{
  gchar *entry = g_strdup_printf(
      "[Desktop Entry]\n"
      "Type=Application\n"
      "Name=%s\n"
      "Comment=Low-power Pomodoro timer for XFCE\n"
      "Exec=xfce4-floating-pomodoro --autostart\n"
      "Icon=xfce4-floating-pomodoro\n"
      "Terminal=false\n"
      "Categories=Utility;Productivity;\n"
      "StartupWMClass=%s\n"
      "X-GNOME-Autostart-enabled=true\n",
      APP_NAME,
      APP_ID);

  gboolean result = g_file_set_contents(path, entry, -1, error);
  g_free(entry);
  return result;
}

gboolean
autostart_set_enabled(gboolean enabled, GError **error)
{
  char *path = autostart_get_path();
  if (enabled) {
    char *dir = g_path_get_dirname(path);
    if (g_mkdir_with_parents(dir, 0755) != 0) {
      g_set_error(error,
                  G_FILE_ERROR,
                  g_file_error_from_errno(errno),
                  "Failed to create autostart directory '%s'",
                  dir);
      g_free(dir);
      g_free(path);
      return FALSE;
    }
    g_free(dir);

    gboolean result = autostart_write_file(path, error);
    g_free(path);
    return result;
  }

  if (g_file_test(path, G_FILE_TEST_EXISTS)) {
    if (g_remove(path) != 0) {
      g_set_error(error,
                  G_FILE_ERROR,
                  g_file_error_from_errno(errno),
                  "Failed to remove autostart file '%s'",
                  path);
      g_free(path);
      return FALSE;
    }
  }

  g_free(path);
  return TRUE;
}
