#pragma once

#include <gio/gio.h>
#include <glib.h>

gboolean ollama_client_detect_available(void);
GPtrArray *ollama_client_list_models_sync(GError **error);
char *ollama_client_chat_sync(const char *model,
                              const char *system_prompt,
                              const char *user_prompt,
                              GCancellable *cancellable,
                              GError **error);
