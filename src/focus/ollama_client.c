#include "focus/ollama_client.h"
#include "config.h"

#if HAVE_CHROME_OLLAMA

#include <libsoup/soup.h>
#include <json-glib/json-glib.h>

gboolean
ollama_client_detect_available(void)
{
  char *path = g_find_program_in_path("ollama");
  if (path == NULL) {
    return FALSE;
  }
  g_free(path);
  return TRUE;
}

GPtrArray *
ollama_client_list_models_sync(GError **error)
{
  gchar *stdout_data = NULL;
  gchar *stderr_data = NULL;
  int status = 0;

  if (!g_spawn_command_line_sync("ollama list",
                                 &stdout_data,
                                 &stderr_data,
                                 &status,
                                 error)) {
    g_free(stdout_data);
    g_free(stderr_data);
    return NULL;
  }

  if (status != 0) {
    g_set_error(error,
                G_IO_ERROR,
                G_IO_ERROR_FAILED,
                "ollama list failed: %s",
                stderr_data != NULL && *stderr_data != '\0'
                    ? stderr_data
                    : "unknown error");
    g_free(stdout_data);
    g_free(stderr_data);
    return NULL;
  }

  GPtrArray *models = g_ptr_array_new_with_free_func(g_free);
  if (stdout_data == NULL) {
    g_free(stderr_data);
    return models;
  }

  gchar **lines = g_strsplit(stdout_data, "\n", -1);
  gboolean header_checked = FALSE;
  for (guint i = 0; lines != NULL && lines[i] != NULL; i++) {
    gchar *line = g_strstrip(lines[i]);
    if (line == NULL || *line == '\0') {
      continue;
    }

    if (!header_checked) {
      header_checked = TRUE;
      if (g_ascii_strncasecmp(line, "name", 4) == 0) {
        continue;
      }
    }

    gchar **tokens = g_strsplit_set(line, " \t", 0);
    if (tokens != NULL && tokens[0] != NULL && *tokens[0] != '\0') {
      g_ptr_array_add(models, g_strdup(tokens[0]));
    }
    g_strfreev(tokens);
  }

  g_strfreev(lines);
  g_free(stdout_data);
  g_free(stderr_data);
  return models;
}

char *
ollama_client_chat_sync(const char *model,
                        const char *system_prompt,
                        const char *user_prompt,
                        GCancellable *cancellable,
                        GError **error)
{
  if (model == NULL || *model == '\0') {
    g_set_error(error,
                G_IO_ERROR,
                G_IO_ERROR_INVALID_ARGUMENT,
                "Ollama model not set");
    return NULL;
  }

  SoupSession *session = soup_session_new();
  SoupMessage *message =
      soup_message_new("POST", "http://127.0.0.1:11434/api/chat");

  JsonBuilder *builder = json_builder_new();
  json_builder_begin_object(builder);
  json_builder_set_member_name(builder, "model");
  json_builder_add_string_value(builder, model);
  json_builder_set_member_name(builder, "messages");
  json_builder_begin_array(builder);

  json_builder_begin_object(builder);
  json_builder_set_member_name(builder, "role");
  json_builder_add_string_value(builder, "system");
  json_builder_set_member_name(builder, "content");
  json_builder_add_string_value(builder, system_prompt != NULL ? system_prompt : "");
  json_builder_end_object(builder);

  json_builder_begin_object(builder);
  json_builder_set_member_name(builder, "role");
  json_builder_add_string_value(builder, "user");
  json_builder_set_member_name(builder, "content");
  json_builder_add_string_value(builder, user_prompt != NULL ? user_prompt : "");
  json_builder_end_object(builder);

  json_builder_end_array(builder);
  json_builder_set_member_name(builder, "stream");
  json_builder_add_boolean_value(builder, FALSE);
  json_builder_end_object(builder);

  JsonGenerator *generator = json_generator_new();
  JsonNode *root = json_builder_get_root(builder);
  json_generator_set_root(generator, root);
  gsize payload_len = 0;
  gchar *payload = json_generator_to_data(generator, &payload_len);

  GBytes *payload_bytes = g_bytes_new_take(payload, payload_len);
  soup_message_set_request_body_from_bytes(message,
                                           "application/json",
                                           payload_bytes);
  g_bytes_unref(payload_bytes);

  GBytes *response_bytes =
      soup_session_send_and_read(session, message, cancellable, error);

  json_node_free(root);
  g_object_unref(generator);
  g_object_unref(builder);

  if (response_bytes == NULL) {
    g_object_unref(message);
    g_object_unref(session);
    return NULL;
  }

  if (soup_message_get_status(message) != SOUP_STATUS_OK) {
    g_set_error(error,
                G_IO_ERROR,
                G_IO_ERROR_FAILED,
                "Ollama HTTP error: %u",
                soup_message_get_status(message));
    g_bytes_unref(response_bytes);
    g_object_unref(message);
    g_object_unref(session);
    return NULL;
  }

  gsize response_len = 0;
  const gchar *response_data = g_bytes_get_data(response_bytes, &response_len);
  gchar *response_text = g_strndup(response_data, response_len);
  g_bytes_unref(response_bytes);

  JsonParser *parser = json_parser_new();
  if (!json_parser_load_from_data(parser,
                                  response_text,
                                  (gssize)response_len,
                                  error)) {
    g_object_unref(parser);
    g_object_unref(message);
    g_object_unref(session);
    g_free(response_text);
    return NULL;
  }

  JsonNode *root_node = json_parser_get_root(parser);
  if (root_node == NULL || !JSON_NODE_HOLDS_OBJECT(root_node)) {
    g_set_error(error,
                G_IO_ERROR,
                G_IO_ERROR_INVALID_DATA,
                "Ollama response missing JSON object");
    g_object_unref(parser);
    g_object_unref(message);
    g_object_unref(session);
    g_free(response_text);
    return NULL;
  }

  JsonObject *root_obj = json_node_get_object(root_node);
  JsonObject *message_obj = json_object_get_object_member(root_obj, "message");
  const char *content = NULL;
  if (message_obj != NULL) {
    content = json_object_get_string_member(message_obj, "content");
  }

  char *result = content != NULL ? g_strdup(content) : NULL;
  if (result == NULL) {
    g_set_error(error,
                G_IO_ERROR,
                G_IO_ERROR_INVALID_DATA,
                "Ollama response missing message content");
  }

  g_object_unref(parser);
  g_object_unref(message);
  g_object_unref(session);
  g_free(response_text);
  return result;
}

#else

gboolean
ollama_client_detect_available(void)
{
  return FALSE;
}

GPtrArray *
ollama_client_list_models_sync(GError **error)
{
  g_set_error(error,
              G_IO_ERROR,
              G_IO_ERROR_NOT_SUPPORTED,
              "Ollama support unavailable (libsoup/json-glib missing)");
  return NULL;
}

char *
ollama_client_chat_sync(const char *model,
                        const char *system_prompt,
                        const char *user_prompt,
                        GCancellable *cancellable,
                        GError **error)
{
  (void)model;
  (void)system_prompt;
  (void)user_prompt;
  (void)cancellable;
  g_set_error(error,
              G_IO_ERROR,
              G_IO_ERROR_NOT_SUPPORTED,
              "Ollama support unavailable (libsoup/json-glib missing)");
  return NULL;
}

#endif
