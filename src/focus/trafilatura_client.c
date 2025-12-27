#include "focus/trafilatura_client.h"

#include <gio/gio.h>

TrafilaturaStatus
trafilatura_client_get_status(void)
{
  char *python = g_find_program_in_path("python3");
  if (python == NULL) {
    return TRAFILATURA_STATUS_NO_PYTHON;
  }
  g_free(python);

  gchar *stdout_data = NULL;
  gchar *stderr_data = NULL;
  int status = 0;
  gboolean ok = g_spawn_command_line_sync("python3 -c \"import trafilatura\"",
                                          &stdout_data,
                                          &stderr_data,
                                          &status,
                                          NULL);

  g_free(stdout_data);
  g_free(stderr_data);

  if (!ok || status != 0) {
    return TRAFILATURA_STATUS_NO_MODULE;
  }

  return TRAFILATURA_STATUS_AVAILABLE;
}
