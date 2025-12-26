#pragma once

#include <glib.h>

gboolean focus_guard_x11_get_active_app(char **app_name_out, char **title_out);
