#include "focus/chrome_cdp_client.h"
#include "config.h"

#if HAVE_LIBSOUP && HAVE_JSON_GLIB

#include <libsoup/soup.h>
#include <json-glib/json-glib.h>
#include <string.h>

#define CHROME_CDP_MAX_TEXT 8000
#define CHROME_CDP_TIMEOUT_SEC 5

typedef struct {
  GMainLoop *loop;
  ChromeCdpPage *page;
  GError *error;
  guint timeout_id;
  gboolean completed;
  SoupWebsocketConnection *connection;
} ChromeCdpWsContext;

static void
chrome_cdp_ws_context_finish(ChromeCdpWsContext *context, GError *error)
{
  if (context == NULL || context->completed) {
    g_clear_error(&error);
    return;
  }

  context->completed = TRUE;
  if (error != NULL) {
    context->error = error;
  }

  if (context->loop != NULL) {
    g_main_loop_quit(context->loop);
  }
}

static gboolean
chrome_cdp_ws_timeout(gpointer user_data)
{
  ChromeCdpWsContext *context = user_data;
  if (context == NULL || context->completed) {
    return G_SOURCE_REMOVE;
  }

  chrome_cdp_ws_context_finish(
      context,
      g_error_new(G_IO_ERROR, G_IO_ERROR_TIMED_OUT, "Chrome CDP timeout"));
  return G_SOURCE_REMOVE;
}

static char *
chrome_cdp_strip_suffix(const char *title)
{
  if (title == NULL) {
    return g_strdup("");
  }

  char *trimmed = g_strstrip(g_strdup(title));
  if (*trimmed == '\0') {
    return trimmed;
  }

  const char *suffixes[] = {" - google chrome", " - chromium", " - chrome", NULL};
  char *lower = g_ascii_strdown(trimmed, -1);
  size_t trimmed_len = strlen(trimmed);

  for (int i = 0; suffixes[i] != NULL; i++) {
    size_t suffix_len = strlen(suffixes[i]);
    if (trimmed_len >= suffix_len && g_str_has_suffix(lower, suffixes[i])) {
      trimmed[trimmed_len - suffix_len] = '\0';
      g_strstrip(trimmed);
      break;
    }
  }

  g_free(lower);
  return trimmed;
}

static char *
chrome_cdp_normalize_title(const char *title, gboolean strip_suffix)
{
  char *base = strip_suffix ? chrome_cdp_strip_suffix(title) : g_strdup(title);
  if (base == NULL) {
    return NULL;
  }

  char *lower = g_ascii_strdown(base, -1);
  g_free(base);
  g_strstrip(lower);
  return lower;
}

static int
chrome_cdp_score_title(const char *window_title, const char *tab_title)
{
  if (window_title == NULL || tab_title == NULL) {
    return 0;
  }

  char *window_norm = chrome_cdp_normalize_title(window_title, TRUE);
  if (window_norm == NULL || *window_norm == '\0') {
    g_free(window_norm);
    return 0;
  }

  char *tab_norm = chrome_cdp_normalize_title(tab_title, FALSE);
  int score = 0;

  if (tab_norm != NULL) {
    if (g_strcmp0(window_norm, tab_norm) == 0) {
      score = 3;
    } else if (g_strstr_len(window_norm, -1, tab_norm) != NULL) {
      score = 2;
    } else if (g_strstr_len(tab_norm, -1, window_norm) != NULL) {
      score = 1;
    }
  }

  g_free(tab_norm);
  g_free(window_norm);
  return score;
}

static const char *
chrome_cdp_json_get_string(JsonObject *object, const char *member)
{
  if (object == NULL || member == NULL) {
    return NULL;
  }
  if (!json_object_has_member(object, member)) {
    return NULL;
  }
  return json_object_get_string_member(object, member);
}

static JsonObject *
chrome_cdp_select_target(JsonArray *array,
                         const char *window_title,
                         const char **out_tab_title,
                         const char **out_ws_url)
{
  if (array == NULL) {
    return NULL;
  }

  JsonObject *best = NULL;
  int best_score = -1;
  const char *best_title = NULL;
  const char *best_ws = NULL;

  guint length = json_array_get_length(array);
  for (guint i = 0; i < length; i++) {
    JsonNode *node = json_array_get_element(array, i);
    if (node == NULL || !JSON_NODE_HOLDS_OBJECT(node)) {
      continue;
    }

    JsonObject *obj = json_node_get_object(node);
    const char *type = chrome_cdp_json_get_string(obj, "type");
    if (type == NULL || g_strcmp0(type, "page") != 0) {
      continue;
    }

    const char *ws_url = chrome_cdp_json_get_string(obj, "webSocketDebuggerUrl");
    if (ws_url == NULL || *ws_url == '\0') {
      continue;
    }

    const char *title = chrome_cdp_json_get_string(obj, "title");
    int score = chrome_cdp_score_title(window_title, title);

    if (best == NULL || score > best_score) {
      best = obj;
      best_score = score;
      best_title = title;
      best_ws = ws_url;
      if (score == 3) {
        break;
      }
    }
  }

  if (out_tab_title != NULL) {
    *out_tab_title = best_title;
  }
  if (out_ws_url != NULL) {
    *out_ws_url = best_ws;
  }
  return best;
}

static gboolean
chrome_cdp_parse_evaluate_result(const char *payload,
                                 ChromeCdpPage **page_out,
                                 GError **error)
{
  JsonParser *parser = json_parser_new();
  if (!json_parser_load_from_data(parser, payload, -1, error)) {
    g_object_unref(parser);
    return FALSE;
  }

  JsonNode *root = json_parser_get_root(parser);
  if (root == NULL || !JSON_NODE_HOLDS_OBJECT(root)) {
    g_set_error(error,
                G_IO_ERROR,
                G_IO_ERROR_INVALID_DATA,
                "Chrome CDP response missing JSON object");
    g_object_unref(parser);
    return FALSE;
  }

  JsonObject *root_obj = json_node_get_object(root);
  if (json_object_has_member(root_obj, "error")) {
    g_set_error(error,
                G_IO_ERROR,
                G_IO_ERROR_FAILED,
                "Chrome CDP returned error");
    g_object_unref(parser);
    return FALSE;
  }

  gint64 id = json_object_get_int_member(root_obj, "id");
  if (id != 1) {
    g_object_unref(parser);
    return FALSE;
  }

  JsonObject *result_obj = json_object_get_object_member(root_obj, "result");
  JsonObject *inner_obj = NULL;
  if (result_obj != NULL) {
    inner_obj = json_object_get_object_member(result_obj, "result");
  }
  JsonNode *value_node = inner_obj != NULL
                             ? json_object_get_member(inner_obj, "value")
                             : NULL;

  if (value_node == NULL || !JSON_NODE_HOLDS_OBJECT(value_node)) {
    g_set_error(error,
                G_IO_ERROR,
                G_IO_ERROR_INVALID_DATA,
                "Chrome CDP response missing value");
    g_object_unref(parser);
    return FALSE;
  }

  JsonObject *value_obj = json_node_get_object(value_node);
  const char *title = chrome_cdp_json_get_string(value_obj, "title");
  const char *url = chrome_cdp_json_get_string(value_obj, "url");
  const char *text = chrome_cdp_json_get_string(value_obj, "text");

  ChromeCdpPage *page = g_new0(ChromeCdpPage, 1);
  page->title = g_strdup(title != NULL ? title : "");
  page->url = g_strdup(url != NULL ? url : "");
  page->text = g_strdup(text != NULL ? text : "");

  *page_out = page;
  g_object_unref(parser);
  return TRUE;
}

static void
chrome_cdp_ws_on_message(SoupWebsocketConnection *connection,
                         SoupWebsocketDataType data_type,
                         GBytes *message,
                         gpointer user_data)
{
  (void)connection;
  ChromeCdpWsContext *context = user_data;
  if (context == NULL || context->completed) {
    return;
  }

  if (data_type != SOUP_WEBSOCKET_DATA_TEXT) {
    return;
  }

  gsize length = 0;
  const gchar *data = g_bytes_get_data(message, &length);
  if (data == NULL || length == 0) {
    return;
  }

  char *payload = g_strndup(data, length);
  ChromeCdpPage *page = NULL;
  GError *error = NULL;
  gboolean parsed = chrome_cdp_parse_evaluate_result(payload, &page, &error);
  g_free(payload);

  if (!parsed) {
    if (error != NULL) {
      chrome_cdp_ws_context_finish(context, error);
    }
    return;
  }

  context->page = page;
  chrome_cdp_ws_context_finish(context, NULL);
}

static void
chrome_cdp_ws_on_closed(SoupWebsocketConnection *connection,
                        gpointer user_data)
{
  (void)connection;
  ChromeCdpWsContext *context = user_data;
  if (context == NULL || context->completed) {
    return;
  }

  chrome_cdp_ws_context_finish(
      context,
      g_error_new(G_IO_ERROR, G_IO_ERROR_FAILED, "Chrome CDP socket closed"));
}

static void
chrome_cdp_ws_send_evaluate(SoupWebsocketConnection *connection)
{
  const char *expression_template =
      "(function(){const max=%d;"
      "let text='';"
      "if(document.body&&document.body.innerText)"
      "{text=document.body.innerText.replace(/\\s+/g,' ').trim();}"
      "if(text.length>max){text=text.slice(0,max);}"
      "return{title:document.title||'',url:location.href||'',text:text};})()";

  char *expression = g_strdup_printf(expression_template, CHROME_CDP_MAX_TEXT);

  JsonBuilder *builder = json_builder_new();
  json_builder_begin_object(builder);
  json_builder_set_member_name(builder, "id");
  json_builder_add_int_value(builder, 1);
  json_builder_set_member_name(builder, "method");
  json_builder_add_string_value(builder, "Runtime.evaluate");
  json_builder_set_member_name(builder, "params");
  json_builder_begin_object(builder);
  json_builder_set_member_name(builder, "expression");
  json_builder_add_string_value(builder, expression);
  json_builder_set_member_name(builder, "returnByValue");
  json_builder_add_boolean_value(builder, TRUE);
  json_builder_end_object(builder);
  json_builder_end_object(builder);

  JsonGenerator *generator = json_generator_new();
  JsonNode *root = json_builder_get_root(builder);
  json_generator_set_root(generator, root);
  gchar *payload = json_generator_to_data(generator, NULL);
  soup_websocket_connection_send_text(connection, payload);

  json_node_free(root);
  g_object_unref(generator);
  g_object_unref(builder);
  g_free(payload);
  g_free(expression);
}

static void
chrome_cdp_ws_on_connected(GObject *source_object,
                           GAsyncResult *res,
                           gpointer user_data)
{
  SoupSession *session = SOUP_SESSION(source_object);
  ChromeCdpWsContext *context = user_data;
  if (context == NULL || context->completed) {
    return;
  }

  GError *error = NULL;
  SoupWebsocketConnection *connection =
      soup_session_websocket_connect_finish(session, res, &error);
  if (connection == NULL) {
    chrome_cdp_ws_context_finish(context, error);
    return;
  }

  context->connection = connection;
  g_signal_connect(connection,
                   "message",
                   G_CALLBACK(chrome_cdp_ws_on_message),
                   context);
  g_signal_connect(connection,
                   "closed",
                   G_CALLBACK(chrome_cdp_ws_on_closed),
                   context);

  chrome_cdp_ws_send_evaluate(connection);
}

static ChromeCdpPage *
chrome_cdp_fetch_page_via_ws(SoupSession *session,
                             const char *ws_url,
                             GCancellable *cancellable,
                             GError **error)
{
  if (session == NULL || ws_url == NULL) {
    g_set_error(error,
                G_IO_ERROR,
                G_IO_ERROR_INVALID_ARGUMENT,
                "Chrome CDP websocket url missing");
    return NULL;
  }

  GMainContext *context = g_main_context_new();
  g_main_context_push_thread_default(context);
  GMainLoop *loop = g_main_loop_new(context, FALSE);

  ChromeCdpWsContext ws_context = {
      .loop = loop,
      .page = NULL,
      .error = NULL,
      .timeout_id = 0,
      .completed = FALSE,
      .connection = NULL,
  };

  SoupMessage *message = soup_message_new("GET", ws_url);
  soup_session_websocket_connect_async(session,
                                       message,
                                       NULL,
                                       NULL,
                                       G_PRIORITY_DEFAULT,
                                       cancellable,
                                       chrome_cdp_ws_on_connected,
                                       &ws_context);

  ws_context.timeout_id =
      g_timeout_add_seconds(CHROME_CDP_TIMEOUT_SEC,
                            chrome_cdp_ws_timeout,
                            &ws_context);

  g_main_loop_run(loop);

  if (ws_context.timeout_id != 0) {
    g_source_remove(ws_context.timeout_id);
  }

  if (ws_context.connection != NULL) {
    soup_websocket_connection_close(ws_context.connection,
                                    SOUP_WEBSOCKET_CLOSE_NORMAL,
                                    NULL);
    g_object_unref(ws_context.connection);
    ws_context.connection = NULL;
  }

  g_object_unref(message);
  g_main_loop_unref(loop);
  g_main_context_pop_thread_default(context);
  g_main_context_unref(context);

  if (ws_context.error != NULL) {
    g_propagate_error(error, ws_context.error);
    return NULL;
  }

  return ws_context.page;
}

ChromeCdpPage *
chrome_cdp_fetch_page_sync(guint port,
                           const char *window_title,
                           GCancellable *cancellable,
                           GError **error)
{
  if (port == 0 || port > 65535) {
    g_set_error(error,
                G_IO_ERROR,
                G_IO_ERROR_INVALID_ARGUMENT,
                "Chrome debug port invalid");
    return NULL;
  }

  char *url = g_strdup_printf("http://127.0.0.1:%u/json/list", port);
  SoupSession *session = soup_session_new();
  SoupMessage *message = soup_message_new("GET", url);
  GBytes *bytes = soup_session_send_and_read(session, message, cancellable, error);
  g_free(url);

  if (bytes == NULL) {
    g_object_unref(message);
    g_object_unref(session);
    return NULL;
  }

  if (soup_message_get_status(message) != SOUP_STATUS_OK) {
    g_set_error(error,
                G_IO_ERROR,
                G_IO_ERROR_FAILED,
                "Chrome CDP HTTP error: %u",
                soup_message_get_status(message));
    g_bytes_unref(bytes);
    g_object_unref(message);
    g_object_unref(session);
    return NULL;
  }

  g_object_unref(message);

  gsize len = 0;
  const gchar *data = g_bytes_get_data(bytes, &len);
  JsonParser *parser = json_parser_new();
  if (!json_parser_load_from_data(parser, data, (gssize)len, error)) {
    g_object_unref(parser);
    g_bytes_unref(bytes);
    g_object_unref(session);
    return NULL;
  }

  JsonNode *root = json_parser_get_root(parser);
  if (root == NULL || !JSON_NODE_HOLDS_ARRAY(root)) {
    g_set_error(error,
                G_IO_ERROR,
                G_IO_ERROR_INVALID_DATA,
                "Chrome CDP response missing tab list");
    g_object_unref(parser);
    g_bytes_unref(bytes);
    g_object_unref(session);
    return NULL;
  }

  JsonArray *array = json_node_get_array(root);
  const char *tab_title = NULL;
  const char *ws_url = NULL;
  chrome_cdp_select_target(array, window_title, &tab_title, &ws_url);

  ChromeCdpPage *page = NULL;
  if (ws_url != NULL) {
    page = chrome_cdp_fetch_page_via_ws(session, ws_url, cancellable, error);
    if (page != NULL && page->title != NULL && *page->title == '\0' &&
        tab_title != NULL) {
      g_free(page->title);
      page->title = g_strdup(tab_title);
    }
  } else {
    g_set_error(error,
                G_IO_ERROR,
                G_IO_ERROR_NOT_FOUND,
                "No Chrome tab available");
  }

  g_object_unref(parser);
  g_bytes_unref(bytes);
  g_object_unref(session);
  return page;
}

void
chrome_cdp_page_free(ChromeCdpPage *page)
{
  if (page == NULL) {
    return;
  }

  g_free(page->title);
  g_free(page->url);
  g_free(page->text);
  g_free(page);
}

#else

ChromeCdpPage *
chrome_cdp_fetch_page_sync(guint port,
                           const char *window_title,
                           GCancellable *cancellable,
                           GError **error)
{
  (void)port;
  (void)window_title;
  (void)cancellable;
  g_set_error(error,
              G_IO_ERROR,
              G_IO_ERROR_NOT_SUPPORTED,
              "Chrome CDP support unavailable (libsoup/json-glib missing)");
  return NULL;
}

void
chrome_cdp_page_free(ChromeCdpPage *page)
{
  if (page == NULL) {
    return;
  }

  g_free(page->title);
  g_free(page->url);
  g_free(page->text);
  g_free(page);
}

#endif
