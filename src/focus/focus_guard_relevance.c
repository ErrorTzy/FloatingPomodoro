#include "focus/focus_guard_internal.h"

#include "focus/chrome_cdp_client.h"
#include "focus/ollama_client.h"

typedef struct {
  GWeakRef window_ref;
  guint64 check_id;
  char *task_title;
  char *window_title;
  char *model;
  guint port;
} FocusGuardRelevanceContext;

typedef struct {
  FocusGuardRelevance verdict;
  ChromeCdpPage *page;
  char *raw_response;
} FocusGuardRelevanceResult;

static FocusGuardRelevance
focus_guard_parse_relevance_response(const char *response)
{
  if (response == NULL) {
    return FOCUS_GUARD_RELEVANCE_UNKNOWN;
  }

  char *lower = g_ascii_strdown(response, -1);
  FocusGuardRelevance verdict = FOCUS_GUARD_RELEVANCE_UNSURE;
  if (g_strstr_len(lower, -1, "clearly irrelevant") != NULL) {
    verdict = FOCUS_GUARD_RELEVANCE_IRRELEVANT;
  } else if (g_strstr_len(lower, -1, "directly relevant") != NULL) {
    verdict = FOCUS_GUARD_RELEVANCE_RELEVANT;
  } else if (g_strstr_len(lower, -1, "not sure") != NULL) {
    verdict = FOCUS_GUARD_RELEVANCE_UNSURE;
  }
  g_free(lower);
  return verdict;
}

static void
focus_guard_relevance_result_free(gpointer data)
{
  FocusGuardRelevanceResult *result = data;
  if (result == NULL) {
    return;
  }

  chrome_cdp_page_free(result->page);
  g_free(result->raw_response);
  g_free(result);
}

static void
focus_guard_relevance_context_free(gpointer data)
{
  FocusGuardRelevanceContext *context = data;
  if (context == NULL) {
    return;
  }

  g_weak_ref_clear(&context->window_ref);
  g_free(context->task_title);
  g_free(context->window_title);
  g_free(context->model);
  g_free(context);
}

static const char *
focus_guard_system_prompt(void)
{
  return "You are a focus assistant that checks if a web page is relevant to the user's task."
         " Reply with exactly one label: directly relevant, not sure, or clearly irrelevant."
         " Use the content inside XML-like tags to decide.\n"
         "\n"
         "Examples:\n"
         "<task-title>Draft Q4 budget report</task-title>\n"
         "<page-title>Q4 Budget — Google Sheets</page-title>\n"
         "<page-content>Revenue, expenses, forecasts, variance notes...</page-content>\n"
         "Answer: directly relevant\n"
         "---\n"
         "<task-title>Draft Q4 budget report</task-title>\n"
         "<page-title>YouTube — Lo-fi hip hop</page-title>\n"
         "<page-content>Playlists, comments, music channels...</page-content>\n"
         "Answer: clearly irrelevant\n"
         "---\n"
         "<task-title>Study GTK4 layout</task-title>\n"
         "<page-title>GTK4 Box and Grid — GNOME Developer</page-title>\n"
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

static char *
focus_guard_escape_xml(const char *text)
{
  if (text == NULL) {
    return g_strdup("");
  }

  return g_markup_escape_text(text, -1);
}

static char *
focus_guard_build_user_prompt(const char *task_title, const ChromeCdpPage *page)
{
  char *task = focus_guard_escape_xml(task_title);
  char *title = focus_guard_escape_xml(page != NULL ? page->title : NULL);
  char *url = focus_guard_escape_xml(page != NULL ? page->url : NULL);
  char *content = focus_guard_escape_xml(page != NULL ? page->text : NULL);

  GString *prompt = g_string_new(NULL);
  g_string_append(prompt, "<context>\n");
  g_string_append_printf(prompt, "  <task-title>%s</task-title>\n", task);
  g_string_append(prompt, "  <page>\n");
  g_string_append_printf(prompt, "    <page-title>%s</page-title>\n", title);
  g_string_append_printf(prompt, "    <page-url>%s</page-url>\n", url);
  g_string_append(prompt, "    <page-content>\n");
  g_string_append(prompt, content);
  g_string_append(prompt, "\n    </page-content>\n");
  g_string_append(prompt, "  </page>\n");
  g_string_append(prompt, "</context>\n\n");
  g_string_append(prompt,
                  "Answer with exactly one label: directly relevant, not sure, or clearly irrelevant.");

  g_free(task);
  g_free(title);
  g_free(url);
  g_free(content);

  return g_string_free(prompt, FALSE);
}

static void
focus_guard_relevance_task(GTask *task,
                           gpointer source_object,
                           gpointer task_data,
                           GCancellable *cancellable)
{
  (void)source_object;
  FocusGuardRelevanceContext *context = task_data;
  if (context == NULL) {
    g_task_return_new_error(task,
                            G_IO_ERROR,
                            G_IO_ERROR_INVALID_ARGUMENT,
                            "Relevance context missing");
    return;
  }

  GError *error = NULL;
  ChromeCdpPage *page = chrome_cdp_fetch_page_sync(context->port,
                                                   context->window_title,
                                                   cancellable,
                                                   &error);
  if (page == NULL) {
    g_task_return_error(task, error);
    return;
  }

  char *user_prompt = focus_guard_build_user_prompt(context->task_title, page);
  char *response =
      ollama_client_chat_sync(context->model,
                              focus_guard_system_prompt(),
                              user_prompt,
                              cancellable,
                              &error);
  g_free(user_prompt);

  if (response == NULL) {
    chrome_cdp_page_free(page);
    g_task_return_error(task, error);
    return;
  }

  FocusGuardRelevanceResult *result = g_new0(FocusGuardRelevanceResult, 1);
  result->raw_response = response;
  result->page = page;
  result->verdict = focus_guard_parse_relevance_response(response);
  g_task_return_pointer(task, result, focus_guard_relevance_result_free);
}

static char *
focus_guard_truncate_label(char *text)
{
  if (text == NULL) {
    return NULL;
  }

  const guint max_chars = 80;
  if (g_utf8_strlen(text, -1) <= max_chars) {
    return text;
  }

  char *shortened = g_utf8_substring(text, 0, max_chars - 3);
  char *result = g_strconcat(shortened, "...", NULL);
  g_free(shortened);
  g_free(text);
  return result;
}

static char *
focus_guard_format_relevance_warning(const ChromeCdpPage *page)
{
  if (page != NULL && page->title != NULL && *page->title != '\0') {
    char *label = g_strdup_printf("Chrome: %s", page->title);
    return focus_guard_truncate_label(label);
  }

  if (page != NULL && page->url != NULL && *page->url != '\0') {
    char *label = g_strdup_printf("Chrome: %s", page->url);
    return focus_guard_truncate_label(label);
  }

  return g_strdup("Chrome off-task");
}

static void
focus_guard_on_relevance_task_complete(GObject *source_object,
                                       GAsyncResult *res,
                                       gpointer user_data)
{
  (void)source_object;
  (void)user_data;
  FocusGuardRelevanceContext *context =
      g_task_get_task_data(G_TASK(res));
  if (context == NULL) {
    return;
  }

  GtkWindow *window = g_weak_ref_get(&context->window_ref);
  if (window == NULL) {
    return;
  }

  AppState *state = g_object_get_data(G_OBJECT(window), "app-state");
  FocusGuard *guard = state != NULL ? state->focus_guard : NULL;
  if (guard == NULL || context->check_id != guard->relevance_check_id) {
    g_object_unref(window);
    return;
  }

  guard->relevance_inflight = FALSE;
  g_clear_object(&guard->relevance_cancellable);

  GError *error = NULL;
  FocusGuardRelevanceResult *result =
      g_task_propagate_pointer(G_TASK(res), &error);

  if (result == NULL || error != NULL) {
    if (error != NULL) {
      g_debug("Chrome relevance check failed: %s", error->message);
      g_clear_error(&error);
    }
    focus_guard_clear_relevance_warning(guard);
    focus_guard_refresh_warning_from_active(guard);
    g_object_unref(window);
    return;
  }

  guard->relevance_state = result->verdict;
  if (result->verdict == FOCUS_GUARD_RELEVANCE_IRRELEVANT) {
    guard->relevance_warning_active = TRUE;
    g_free(guard->relevance_warning_text);
    guard->relevance_warning_text =
        focus_guard_format_relevance_warning(result->page);
  } else {
    focus_guard_clear_relevance_warning(guard);
  }

  focus_guard_refresh_warning_from_active(guard);
  focus_guard_relevance_result_free(result);
  g_object_unref(window);
}

void
focus_guard_clear_relevance_warning(FocusGuard *guard)
{
  if (guard == NULL) {
    return;
  }

  guard->relevance_warning_active = FALSE;
  guard->relevance_state = FOCUS_GUARD_RELEVANCE_UNKNOWN;
  g_clear_pointer(&guard->relevance_warning_text, g_free);
}

void
focus_guard_start_relevance_check(FocusGuard *guard,
                                  const char *window_title,
                                  const char *task_title)
{
  if (guard == NULL || guard->state == NULL) {
    return;
  }

  if (guard->relevance_inflight) {
    return;
  }

  if (guard->config.ollama_model == NULL ||
      *guard->config.ollama_model == '\0') {
    return;
  }

  if (task_title == NULL || *task_title == '\0') {
    return;
  }

  guard->relevance_inflight = TRUE;
  guard->relevance_check_id++;
  guard->relevance_cancellable = g_cancellable_new();

  FocusGuardRelevanceContext *context = g_new0(FocusGuardRelevanceContext, 1);
  g_weak_ref_init(&context->window_ref, G_OBJECT(guard->state->window));
  context->check_id = guard->relevance_check_id;
  context->task_title = g_strdup(task_title);
  context->window_title = g_strdup(window_title);
  context->model = g_strdup(guard->config.ollama_model);
  context->port = guard->config.chrome_debug_port;

  GTask *task = g_task_new(NULL,
                           guard->relevance_cancellable,
                           focus_guard_on_relevance_task_complete,
                           NULL);
  g_task_set_task_data(task, context, focus_guard_relevance_context_free);
  g_task_run_in_thread(task, focus_guard_relevance_task);
  g_object_unref(task);
}

void
focus_guard_cancel_relevance_check(FocusGuard *guard)
{
  if (guard == NULL) {
    return;
  }

  if (guard->relevance_cancellable != NULL) {
    g_cancellable_cancel(guard->relevance_cancellable);
    g_clear_object(&guard->relevance_cancellable);
  }

  guard->relevance_inflight = FALSE;
  guard->relevance_check_id++;
}
