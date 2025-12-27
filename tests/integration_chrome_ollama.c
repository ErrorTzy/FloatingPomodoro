#include <gio/gio.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <libsoup/soup.h>

#include "focus/chrome_cdp_client.h"
#include "focus/ollama_client.h"

static gboolean
port_is_open(guint port)
{
  GError *error = NULL;
  GSocket *socket =
      g_socket_new(G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_TCP, &error);
  if (socket == NULL) {
    g_clear_error(&error);
    return FALSE;
  }

  GInetAddress *address = g_inet_address_new_loopback(G_SOCKET_FAMILY_IPV4);
  GSocketAddress *sock_addr = g_inet_socket_address_new(address, port);
  gboolean connected = g_socket_connect(socket, sock_addr, NULL, &error);

  g_clear_error(&error);
  g_object_unref(sock_addr);
  g_object_unref(address);
  g_object_unref(socket);
  return connected;
}

static guint
find_free_port(guint start_port, guint end_port)
{
  for (guint port = start_port; port <= end_port; port++) {
    if (!port_is_open(port)) {
      return port;
    }
  }
  return 0;
}

static char *
find_chrome_binary(void)
{
  const char *candidates[] = {
      "google-chrome",
      "google-chrome-stable",
      "chromium",
      "chromium-browser",
      NULL};

  for (int i = 0; candidates[i] != NULL; i++) {
    char *path = g_find_program_in_path(candidates[i]);
    if (path != NULL) {
      return path;
    }
  }

  return NULL;
}

static gboolean
http_get_ok(const char *url, GError **error)
{
  SoupSession *session = soup_session_new();
  SoupMessage *message = soup_message_new("GET", url);
  GBytes *bytes = soup_session_send_and_read(session, message, NULL, error);
  if (bytes == NULL) {
    g_object_unref(message);
    g_object_unref(session);
    return FALSE;
  }

  gboolean ok = soup_message_get_status(message) == SOUP_STATUS_OK;
  if (!ok && error != NULL) {
    g_set_error(error,
                G_IO_ERROR,
                G_IO_ERROR_FAILED,
                "HTTP status %u",
                soup_message_get_status(message));
  }

  g_bytes_unref(bytes);
  g_object_unref(message);
  g_object_unref(session);
  return ok;
}

static gboolean
chrome_open_url(guint port, const char *url, GError **error)
{
  char *escaped = g_uri_escape_string(url, NULL, TRUE);

  char *endpoint = g_strdup_printf("http://127.0.0.1:%u/json/new?%s",
                                   port,
                                   escaped);
  gboolean ok = http_get_ok(endpoint, error);
  g_free(endpoint);

  if (!ok) {
    g_clear_error(error);
    endpoint = g_strdup_printf("http://127.0.0.1:%u/json/new?url=%s",
                               port,
                               escaped);
    ok = http_get_ok(endpoint, error);
    g_free(endpoint);
  }

  g_free(escaped);
  return ok;
}

static gboolean
chrome_wait_for_ready(guint port, guint timeout_ms)
{
  char *endpoint = g_strdup_printf("http://127.0.0.1:%u/json/version", port);
  guint attempts = timeout_ms / 200;
  for (guint i = 0; i < attempts; i++) {
    if (http_get_ok(endpoint, NULL)) {
      g_free(endpoint);
      return TRUE;
    }
    g_usleep(200000);
  }
  g_free(endpoint);
  return FALSE;
}

static gboolean
spawn_chrome(const char *chrome_bin,
             guint port,
             const char *url,
             char **user_data_dir,
             GPid *pid_out,
             GError **error)
{
  if (chrome_bin == NULL || url == NULL) {
    g_set_error(error,
                G_IO_ERROR,
                G_IO_ERROR_INVALID_ARGUMENT,
                "Chrome binary or URL missing");
    return FALSE;
  }

  char *profile_dir = g_dir_make_tmp("xfce4-pomodoro-chrome-XXXXXX", error);
  if (profile_dir == NULL) {
    return FALSE;
  }

  char *port_arg = g_strdup_printf("--remote-debugging-port=%u", port);
  char *user_data_arg = g_strdup_printf("--user-data-dir=%s", profile_dir);

  char *argv[] = {
      (char *)chrome_bin,
      port_arg,
      user_data_arg,
      "--no-first-run",
      "--no-default-browser-check",
      "--disable-extensions",
      "--headless=new",
      "--disable-gpu",
      "--window-size=1200,800",
      (char *)url,
      NULL};

  gboolean ok = g_spawn_async(NULL,
                              argv,
                              NULL,
                              G_SPAWN_DO_NOT_REAP_CHILD,
                              NULL,
                              NULL,
                              pid_out,
                              error);

  g_free(port_arg);
  g_free(user_data_arg);

  if (!ok) {
    g_clear_pointer(&profile_dir, g_free);
    return FALSE;
  }

  *user_data_dir = profile_dir;
  return TRUE;
}

static void
terminate_process(GPid pid)
{
  if (pid <= 0) {
    return;
  }

  kill(pid, SIGTERM);
  for (int i = 0; i < 20; i++) {
    int status = 0;
    pid_t result = waitpid(pid, &status, WNOHANG);
    if (result > 0) {
      g_spawn_close_pid(pid);
      return;
    }
    g_usleep(100000);
  }

  kill(pid, SIGKILL);
  waitpid(pid, NULL, 0);
  g_spawn_close_pid(pid);
}

static void
remove_dir_recursive(const char *path)
{
  if (path == NULL) {
    return;
  }

  GDir *dir = g_dir_open(path, 0, NULL);
  if (dir == NULL) {
    g_remove(path);
    return;
  }

  const char *entry = NULL;
  while ((entry = g_dir_read_name(dir)) != NULL) {
    if (g_strcmp0(entry, ".") == 0 || g_strcmp0(entry, "..") == 0) {
      continue;
    }
    char *child = g_build_filename(path, entry, NULL);
    if (g_file_test(child, G_FILE_TEST_IS_DIR)) {
      remove_dir_recursive(child);
      g_rmdir(child);
    } else {
      g_remove(child);
    }
    g_free(child);
  }

  g_dir_close(dir);
  g_rmdir(path);
}

static char *
get_ollama_model(void)
{
  const char *env = g_getenv("POMODORO_TEST_OLLAMA_MODEL");
  if (env != NULL && *env != '\0') {
    return g_strdup(env);
  }

  GError *error = NULL;
  GPtrArray *models = ollama_client_list_models_sync(&error);
  if (models == NULL) {
    g_clear_error(&error);
    return NULL;
  }

  char *model = NULL;
  if (models->len > 0) {
    model = g_strdup(g_ptr_array_index(models, 0));
  }

  g_ptr_array_unref(models);
  return model;
}

static const char *
system_prompt(void)
{
  return "You are a focus assistant that checks if a web page is relevant to the user's task."
         " Reply with exactly one label: directly relevant, not sure, or clearly irrelevant."
         " Use the content inside XML-like tags to decide.\n"
         "\n"
         "Examples:\n"
         "<task-title>Draft Q4 budget report</task-title>\n"
         "<page-title>Q4 Budget - Google Sheets</page-title>\n"
         "<page-content>Revenue, expenses, forecasts, variance notes...</page-content>\n"
         "Answer: directly relevant\n"
         "---\n"
         "<task-title>Draft Q4 budget report</task-title>\n"
         "<page-title>YouTube - Lo-fi hip hop</page-title>\n"
         "<page-content>Playlists, comments, music channels...</page-content>\n"
         "Answer: clearly irrelevant\n"
         "---\n"
         "<task-title>Study GTK4 layout</task-title>\n"
         "<page-title>GTK4 Box and Grid - GNOME Developer</page-title>\n"
         "<page-content>GtkBox, GtkGrid, layout examples...</page-content>\n"
         "Answer: directly relevant\n"
         "---\n"
         "<task-title>Plan a workshop agenda</task-title>\n"
         "<page-title>Hacker News</page-title>\n"
         "<page-content>Top stories, comments, unrelated news...</page-content>\n"
         "Answer: not sure\n"
         "\n"
         "Return only the label.";
}

static gboolean
response_has_label(const char *response)
{
  if (response == NULL) {
    return FALSE;
  }

  char *lower = g_ascii_strdown(response, -1);
  gboolean ok =
      g_strstr_len(lower, -1, "directly relevant") != NULL ||
      g_strstr_len(lower, -1, "clearly irrelevant") != NULL ||
      g_strstr_len(lower, -1, "not sure") != NULL;
  g_free(lower);
  return ok;
}

static void
test_chrome_ollama_pipeline(void)
{
  if (g_getenv("POMODORO_RUN_INTEGRATION_TESTS") == NULL) {
    g_test_skip("Set POMODORO_RUN_INTEGRATION_TESTS=1 to enable integration tests.");
    return;
  }

  if (!ollama_client_detect_available()) {
    g_test_skip("Ollama not available on PATH.");
    return;
  }

  char *chrome_bin = find_chrome_binary();
  if (chrome_bin == NULL) {
    g_test_skip("Chrome/Chromium binary not found.");
    return;
  }

  guint port = 9222;
  const char *title = "Pomodoro Integration Test";
  const char *task_title = "Write integration tests for Chrome and Ollama";
  const char *body =
      "Integration test for Chrome and Ollama. Task: Write integration tests for Chrome and Ollama.";

  char *temp_dir = g_dir_make_tmp("xfce4-pomodoro-test-XXXXXX", NULL);
  g_assert_nonnull(temp_dir);

  char *html_path = g_build_filename(temp_dir, "index.html", NULL);
  char *html = g_strdup_printf(
      "<!doctype html><html><head><title>%s</title></head><body>%s</body></html>",
      title,
      body);
  g_assert_true(g_file_set_contents(html_path, html, -1, NULL));

  char *file_url = g_filename_to_uri(html_path, NULL, NULL);
  g_assert_nonnull(file_url);

  gboolean spawned = FALSE;
  GPid chrome_pid = 0;
  char *profile_dir = NULL;

  gboolean using_existing = port_is_open(port);
  gboolean opened = FALSE;

  if (using_existing) {
    g_assert_true(chrome_wait_for_ready(port, 5000));

    GError *open_error = NULL;
    for (int attempt = 0; attempt < 10 && !opened; attempt++) {
      opened = chrome_open_url(port, file_url, &open_error);
      if (!opened) {
        if (open_error != NULL) {
          g_test_message("Open URL attempt %d failed on 9222: %s",
                         attempt + 1,
                         open_error->message);
          g_clear_error(&open_error);
        }
        g_usleep(200000);
      }
    }
  }

  if (!using_existing || !opened) {
    port = 0;
    port = find_free_port(9223, 9240);
    g_assert_cmpuint(port, >, 0);

    GError *error = NULL;
    g_assert_true(spawn_chrome(chrome_bin,
                               port,
                               file_url,
                               &profile_dir,
                               &chrome_pid,
                               &error));
    if (error != NULL) {
      g_test_message("Chrome spawn error: %s", error->message);
      g_clear_error(&error);
    }
    spawned = TRUE;
    g_assert_true(chrome_wait_for_ready(port, 10000));
  } else {
    g_test_message("Using existing Chrome on port 9222 for integration test.");
  }

  ChromeCdpPage *page = NULL;
  for (int attempt = 0; attempt < 50 && page == NULL; attempt++) {
    GError *error = NULL;
    page = chrome_cdp_fetch_page_sync(port, title, NULL, &error);
    if (page == NULL) {
      if (error != NULL) {
        g_test_message("CDP attempt %d failed: %s", attempt + 1, error->message);
        g_clear_error(&error);
      }
      g_usleep(200000);
    }
  }

  g_assert_nonnull(page);
  g_assert_nonnull(page->text);
  g_assert_true(g_strstr_len(page->text, -1, body) != NULL);

  char *model = get_ollama_model();
  if (model == NULL) {
    chrome_cdp_page_free(page);
    g_test_skip("No Ollama models available.");
    goto cleanup;
  }

  char *user_prompt = g_strdup_printf("<context>\n"
                                      "  <task-title>%s</task-title>\n"
                                      "  <page>\n"
                                      "    <page-title>%s</page-title>\n"
                                      "    <page-url>%s</page-url>\n"
                                      "    <page-content>\n"
                                      "%s\n"
                                      "    </page-content>\n"
                                      "  </page>\n"
                                      "</context>\n"
                                      "\n"
                                      "Answer with exactly one label: directly relevant, not sure, or clearly irrelevant.",
                                      task_title,
                                      page->title != NULL ? page->title : "",
                                      page->url != NULL ? page->url : "",
                                      page->text != NULL ? page->text : "");

  GError *ollama_error = NULL;
  char *response =
      ollama_client_chat_sync(model, system_prompt(), user_prompt, NULL, &ollama_error);
  if (ollama_error != NULL) {
    g_test_message("Ollama error: %s", ollama_error->message);
  }
  g_assert_no_error(ollama_error);
  g_assert_nonnull(response);
  g_assert_true(response_has_label(response));

  if (g_getenv("POMODORO_TEST_OLLAMA_STRICT") != NULL) {
    char *lower = g_ascii_strdown(response, -1);
    g_assert_true(g_strstr_len(lower, -1, "directly relevant") != NULL);
    g_free(lower);
  }

  g_free(response);
  g_free(user_prompt);
  g_free(model);
  chrome_cdp_page_free(page);

cleanup:
  if (spawned) {
    terminate_process(chrome_pid);
  }

  if (profile_dir != NULL) {
    remove_dir_recursive(profile_dir);
  }

  remove_dir_recursive(temp_dir);
  g_free(file_url);
  g_free(html);
  g_free(html_path);
  g_free(temp_dir);
  g_free(chrome_bin);
}

int
main(int argc, char **argv)
{
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/integration/chrome_ollama", test_chrome_ollama_pipeline);
  return g_test_run();
}
