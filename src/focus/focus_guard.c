#include "focus/focus_guard.h"

#include "core/pomodoro_timer.h"
#include "core/task_store.h"
#include "focus/focus_guard_x11.h"
#include "overlay/overlay_window.h"

typedef struct {
  char *display_name;
  gint64 usec_total;
} FocusGuardUsage;

struct _FocusGuard {
  AppState *state;
  FocusGuardConfig config;
  char **blacklist_norm;
  GHashTable *usage;
  guint tick_source_id;
  gint64 last_tick_us;
  gboolean warning_active;
  char *warning_app;
  gboolean usage_dirty;
};

static gint
focus_guard_usage_compare_desc(gconstpointer a, gconstpointer b)
{
  const FocusGuardUsage *left = a;
  const FocusGuardUsage *right = b;
  if (left == NULL || right == NULL) {
    return 0;
  }
  if (left->usec_total == right->usec_total) {
    return 0;
  }
  return left->usec_total < right->usec_total ? 1 : -1;
}

static void
focus_guard_usage_free(gpointer data)
{
  FocusGuardUsage *usage = data;
  if (usage == NULL) {
    return;
  }

  g_free(usage->display_name);
  g_free(usage);
}

static void
focus_guard_clear_warning(FocusGuard *guard)
{
  if (guard == NULL || guard->state == NULL) {
    return;
  }

  if (!guard->warning_active) {
    return;
  }

  guard->warning_active = FALSE;
  g_clear_pointer(&guard->warning_app, g_free);
  overlay_window_set_warning(guard->state, FALSE, NULL);
}

static gboolean
focus_guard_should_track(const FocusGuard *guard)
{
  if (guard == NULL || guard->state == NULL || guard->state->timer == NULL ||
      guard->state->store == NULL) {
    return FALSE;
  }

  PomodoroTimerState run_state = pomodoro_timer_get_state(guard->state->timer);
  if (run_state != POMODORO_TIMER_RUNNING) {
    return FALSE;
  }

  PomodoroPhase phase = pomodoro_timer_get_phase(guard->state->timer);
  if (phase != POMODORO_PHASE_FOCUS) {
    return FALSE;
  }

  return task_store_get_active(guard->state->store) != NULL;
}


static void
focus_guard_build_blacklist(FocusGuard *guard)
{
  if (guard == NULL) {
    return;
  }

  g_strfreev(guard->blacklist_norm);
  guard->blacklist_norm = NULL;

  if (guard->config.blacklist == NULL) {
    guard->blacklist_norm = g_new0(char *, 1);
    return;
  }

  gsize count = g_strv_length(guard->config.blacklist);
  guard->blacklist_norm = g_new0(char *, count + 1);

  for (gsize i = 0; i < count; i++) {
    const char *value = guard->config.blacklist[i];
    if (value == NULL) {
      continue;
    }
    guard->blacklist_norm[i] = g_ascii_strdown(value, -1);
  }
}

static gboolean
focus_guard_is_blacklisted(FocusGuard *guard, const char *app_key)
{
  if (guard == NULL || app_key == NULL || guard->blacklist_norm == NULL) {
    return FALSE;
  }

  for (gsize i = 0; guard->blacklist_norm[i] != NULL; i++) {
    const char *value = guard->blacklist_norm[i];
    if (value == NULL || *value == '\0') {
      continue;
    }
    if (g_strstr_len(app_key, -1, value) != NULL) {
      return TRUE;
    }
  }

  return FALSE;
}

static FocusGuardUsage *
focus_guard_usage_get_or_create(FocusGuard *guard,
                                const char *key,
                                const char *display)
{
  if (guard == NULL || guard->usage == NULL || key == NULL) {
    return NULL;
  }

  FocusGuardUsage *usage = g_hash_table_lookup(guard->usage, key);
  if (usage != NULL) {
    return usage;
  }

  usage = g_new0(FocusGuardUsage, 1);
  usage->display_name = g_strdup(display ? display : key);
  usage->usec_total = 0;
  g_hash_table_insert(guard->usage, g_strdup(key), usage);
  return usage;
}

static char *
focus_guard_format_duration(gint64 seconds)
{
  if (seconds < 0) {
    seconds = 0;
  }

  gint64 hours = seconds / 3600;
  gint64 minutes = (seconds % 3600) / 60;
  gint64 secs = seconds % 60;

  if (hours > 0) {
    return g_strdup_printf("%" G_GINT64_FORMAT "h %02" G_GINT64_FORMAT "m",
                           hours,
                           minutes);
  }

  return g_strdup_printf("%" G_GINT64_FORMAT "m %02" G_GINT64_FORMAT "s",
                         minutes,
                         secs);
}

static void
focus_guard_clear_list(GtkWidget *list)
{
  if (list == NULL) {
    return;
  }

  GtkWidget *child = gtk_widget_get_first_child(list);
  while (child != NULL) {
    GtkWidget *next = gtk_widget_get_next_sibling(child);
    gtk_list_box_remove(GTK_LIST_BOX(list), child);
    child = next;
  }
}

static void
focus_guard_update_stats_ui(FocusGuard *guard)
{
  if (guard == NULL || guard->state == NULL || guard->usage == NULL ||
      guard->state->focus_stats_list == NULL) {
    return;
  }

  if (!guard->usage_dirty) {
    return;
  }

  GPtrArray *entries = g_ptr_array_new();
  GHashTableIter iter;
  gpointer key = NULL;
  gpointer value = NULL;
  g_hash_table_iter_init(&iter, guard->usage);
  while (g_hash_table_iter_next(&iter, &key, &value)) {
    FocusGuardUsage *usage = value;
    if (usage == NULL || usage->usec_total <= 0) {
      continue;
    }
    g_ptr_array_add(entries, usage);
  }

  g_ptr_array_sort(entries, focus_guard_usage_compare_desc);

  focus_guard_clear_list(guard->state->focus_stats_list);

  const guint max_rows = 5;
  guint shown = 0;
  for (guint i = 0; i < entries->len && shown < max_rows; i++) {
    FocusGuardUsage *usage = g_ptr_array_index(entries, i);
    if (usage == NULL) {
      continue;
    }

    GtkWidget *row = gtk_list_box_row_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_hexpand(box, TRUE);

    GtkWidget *app_label = gtk_label_new(usage->display_name);
    gtk_widget_add_css_class(app_label, "focus-guard-app");
    gtk_widget_set_halign(app_label, GTK_ALIGN_START);
    gtk_label_set_ellipsize(GTK_LABEL(app_label), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(app_label, TRUE);

    char *duration = focus_guard_format_duration(usage->usec_total / G_USEC_PER_SEC);
    GtkWidget *time_label = gtk_label_new(duration);
    gtk_widget_add_css_class(time_label, "focus-guard-time");
    gtk_widget_set_halign(time_label, GTK_ALIGN_END);
    g_free(duration);

    gtk_box_append(GTK_BOX(box), app_label);
    gtk_box_append(GTK_BOX(box), time_label);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
    gtk_list_box_append(GTK_LIST_BOX(guard->state->focus_stats_list), row);
    shown++;
  }

  if (guard->state->focus_stats_empty_label != NULL) {
    gtk_widget_set_visible(guard->state->focus_stats_empty_label, shown == 0);
  }

  guard->usage_dirty = FALSE;
  g_ptr_array_free(entries, TRUE);
}

static void
focus_guard_trigger_warning(FocusGuard *guard, const char *app_name)
{
  if (guard == NULL || guard->state == NULL || app_name == NULL) {
    return;
  }

  if (!guard->warning_active ||
      g_strcmp0(guard->warning_app, app_name) != 0) {
    g_free(guard->warning_app);
    guard->warning_app = g_strdup(app_name);
  }

  guard->warning_active = TRUE;

  if (!overlay_window_is_visible(guard->state)) {
    overlay_window_set_visible(guard->state, TRUE);
  }

  char *message = g_strdup_printf("Blocked: %s", app_name);
  overlay_window_set_warning(guard->state, TRUE, message);
  g_free(message);
}

static gboolean
focus_guard_on_tick(gpointer data)
{
  FocusGuard *guard = data;
  if (guard == NULL) {
    return G_SOURCE_CONTINUE;
  }

  gint64 now_us = g_get_monotonic_time();
  gint64 elapsed_us = guard->last_tick_us > 0 ? now_us - guard->last_tick_us : 0;
  guard->last_tick_us = now_us;

  if (elapsed_us < 0) {
    elapsed_us = 0;
  }

  gint64 max_elapsed =
      (gint64)guard->config.detection_interval_seconds * 3 * G_USEC_PER_SEC;
  if (max_elapsed > 0 && elapsed_us > max_elapsed) {
    elapsed_us = (gint64)guard->config.detection_interval_seconds * G_USEC_PER_SEC;
  }

  char *app_name = NULL;
  gboolean has_app = focus_guard_x11_get_active_app(&app_name, NULL);
  char *app_key = NULL;

  if (has_app && app_name != NULL) {
    app_key = g_ascii_strdown(app_name, -1);
  }

  gboolean tracking = focus_guard_should_track(guard);
  if (tracking && elapsed_us > 0 && app_key != NULL) {
    FocusGuardUsage *usage =
        focus_guard_usage_get_or_create(guard, app_key, app_name);
    if (usage != NULL) {
      usage->usec_total += elapsed_us;
      guard->usage_dirty = TRUE;
    }
  }

  if (!tracking || !guard->config.warnings_enabled || app_key == NULL) {
    focus_guard_clear_warning(guard);
  } else if (focus_guard_is_blacklisted(guard, app_key)) {
    focus_guard_trigger_warning(guard, app_name);
  } else {
    focus_guard_clear_warning(guard);
  }

  focus_guard_update_stats_ui(guard);

  g_free(app_key);
  g_free(app_name);

  return G_SOURCE_CONTINUE;
}

static void
focus_guard_restart_timer(FocusGuard *guard)
{
  if (guard == NULL) {
    return;
  }

  if (guard->tick_source_id != 0) {
    g_source_remove(guard->tick_source_id);
    guard->tick_source_id = 0;
  }

  guard->last_tick_us = 0;

  guint interval = guard->config.detection_interval_seconds;
  if (interval < 1) {
    interval = 1;
  }

  guard->tick_source_id = g_timeout_add_seconds_full(G_PRIORITY_LOW,
                                                     interval,
                                                     focus_guard_on_tick,
                                                     guard,
                                                     NULL);
  focus_guard_on_tick(guard);
}

FocusGuard *
focus_guard_create(AppState *state, FocusGuardConfig config)
{
  if (state == NULL) {
    focus_guard_config_clear(&config);
    return NULL;
  }

  FocusGuard *guard = g_new0(FocusGuard, 1);
  guard->state = state;
  guard->usage = g_hash_table_new_full(g_str_hash,
                                       g_str_equal,
                                       g_free,
                                       focus_guard_usage_free);
  guard->config = focus_guard_config_copy(&config);
  focus_guard_build_blacklist(guard);
  focus_guard_restart_timer(guard);
  return guard;
}

void
focus_guard_destroy(FocusGuard *guard)
{
  if (guard == NULL) {
    return;
  }

  if (guard->tick_source_id != 0) {
    g_source_remove(guard->tick_source_id);
    guard->tick_source_id = 0;
  }

  if (guard->usage != NULL) {
    g_hash_table_destroy(guard->usage);
  }

  g_strfreev(guard->blacklist_norm);
  focus_guard_config_clear(&guard->config);
  g_free(guard->warning_app);
  g_free(guard);
}

void
focus_guard_apply_config(FocusGuard *guard, FocusGuardConfig config)
{
  if (guard == NULL) {
    focus_guard_config_clear(&config);
    return;
  }

  focus_guard_config_clear(&guard->config);
  guard->config = focus_guard_config_copy(&config);
  focus_guard_build_blacklist(guard);

  focus_guard_restart_timer(guard);

  if (!guard->config.warnings_enabled) {
    focus_guard_clear_warning(guard);
  }
}

FocusGuardConfig
focus_guard_get_config(const FocusGuard *guard)
{
  if (guard == NULL) {
    return focus_guard_config_default();
  }

  return focus_guard_config_copy(&guard->config);
}
