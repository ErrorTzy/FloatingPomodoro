#include "focus/trafilatura_client.h"

#include <gio/gio.h>

static char *
trafilatura_client_expand_path(const char *path)
{
  if (path == NULL) {
    return NULL;
  }

  if (path[0] == '~') {
    if (path[1] == '\0') {
      return g_strdup(g_get_home_dir());
    }
    if (path[1] == G_DIR_SEPARATOR) {
      return g_build_filename(g_get_home_dir(), path + 2, NULL);
    }
  }

  return g_strdup(path);
}

static gboolean
trafilatura_client_is_path(const char *path)
{
  if (path == NULL) {
    return FALSE;
  }

  return g_strrstr(path, G_DIR_SEPARATOR_S) != NULL || path[0] == '~';
}

static char *
trafilatura_client_resolve_python(const char *python_path)
{
  if (python_path == NULL || *python_path == '\0') {
    return g_find_program_in_path("python3");
  }

  if (trafilatura_client_is_path(python_path)) {
    char *expanded = trafilatura_client_expand_path(python_path);
    if (expanded == NULL) {
      return NULL;
    }

    if (!g_file_test(expanded, G_FILE_TEST_IS_EXECUTABLE)) {
      g_free(expanded);
      return NULL;
    }

    return expanded;
  }

  return g_find_program_in_path(python_path);
}

TrafilaturaStatus
trafilatura_client_get_status(const char *python_path)
{
  char *trimmed = NULL;
  if (python_path != NULL) {
    trimmed = g_strstrip(g_strdup(python_path));
    if (*trimmed == '\0') {
      g_free(trimmed);
      trimmed = NULL;
    }
  }

  char *python = trafilatura_client_resolve_python(trimmed);
  g_free(trimmed);
  if (python == NULL) {
    return TRAFILATURA_STATUS_NO_PYTHON;
  }

  gchar *stdout_data = NULL;
  gchar *stderr_data = NULL;
  int status = 0;
  char *argv[] = {
      python,
      "-c",
      "import trafilatura",
      NULL,
  };
  gboolean ok = g_spawn_sync(NULL,
                             argv,
                             NULL,
                             0,
                             NULL,
                             NULL,
                             &stdout_data,
                             &stderr_data,
                             &status,
                             NULL);

  g_free(stdout_data);
  g_free(stderr_data);
  g_free(python);

  if (!ok || status != 0) {
    return TRAFILATURA_STATUS_NO_MODULE;
  }

  return TRAFILATURA_STATUS_AVAILABLE;
}
