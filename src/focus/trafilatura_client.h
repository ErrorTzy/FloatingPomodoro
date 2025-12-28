#pragma once

#include <glib.h>

typedef enum {
  TRAFILATURA_STATUS_AVAILABLE = 0,
  TRAFILATURA_STATUS_NO_PYTHON = 1,
  TRAFILATURA_STATUS_NO_MODULE = 2
} TrafilaturaStatus;

TrafilaturaStatus trafilatura_client_get_status(const char *python_path);
