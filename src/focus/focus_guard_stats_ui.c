#include "focus/focus_guard_internal.h"

static gint
focus_guard_usage_compare_desc(gconstpointer a, gconstpointer b)
{
  const FocusGuardUsage *left = *(const FocusGuardUsage *const *)a;
  const FocusGuardUsage *right = *(const FocusGuardUsage *const *)b;
  if (left == right) {
    return 0;
  }
  if (left == NULL) {
    return 1;
  }
  if (right == NULL) {
    return -1;
  }
  if (left->usec_total < right->usec_total) {
    return 1;
  }
  if (left->usec_total > right->usec_total) {
    return -1;
  }

  const char *left_name = left->display_name != NULL ? left->display_name : "";
  const char *right_name = right->display_name != NULL ? right->display_name : "";
  gint name_cmp = g_ascii_strcasecmp(left_name, right_name);
  if (name_cmp != 0) {
    return name_cmp;
  }
  return g_strcmp0(left_name, right_name);
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
focus_guard_update_stats_header(FocusGuard *guard)
{
  if (guard == NULL || guard->state == NULL) {
    return;
  }

  if (guard->state->focus_stats_context_label != NULL) {
    const char *text = NULL;
    if (guard->view == FOCUS_GUARD_VIEW_TASK) {
      if (guard->view_task_title != NULL) {
        char *label = g_strdup_printf("Task: %s", guard->view_task_title);
        gtk_label_set_text(GTK_LABEL(guard->state->focus_stats_context_label), label);
        g_free(label);
      } else {
        gtk_label_set_text(GTK_LABEL(guard->state->focus_stats_context_label),
                           "Task stats");
      }
    } else {
      if (!guard->config.global_stats_enabled) {
        text = "Global stats (disabled)";
      } else {
        text = "Global stats";
      }
      gtk_label_set_text(GTK_LABEL(guard->state->focus_stats_context_label),
                         text);
    }
  }

  if (guard->state->focus_stats_day_label != NULL) {
    const char *day_text = guard->day_label != NULL ? guard->day_label : "Today";
    gtk_label_set_text(GTK_LABEL(guard->state->focus_stats_day_label), day_text);
  }
}

void
focus_guard_update_stats_ui(FocusGuard *guard)
{
  if (guard == NULL || guard->state == NULL ||
      guard->state->focus_stats_list == NULL) {
    return;
  }

  if (!guard->usage_dirty) {
    return;
  }

  focus_guard_refresh_day(guard);
  focus_guard_update_stats_header(guard);

  const char *empty_text = NULL;
  GHashTable *source = NULL;
  if (guard->view == FOCUS_GUARD_VIEW_TASK) {
    source = guard->usage_task_view;
    empty_text = guard->view_task_id != NULL
                     ? "No app activity yet for this task."
                     : "Select a task to view stats.";
  } else if (!guard->config.global_stats_enabled) {
    source = NULL;
    empty_text = "Global stats disabled.";
  } else {
    source = guard->usage_global;
    empty_text = "No app activity yet.";
  }

  GPtrArray *entries = g_ptr_array_new();
  if (source != NULL) {
    GHashTableIter iter;
    gpointer key = NULL;
    gpointer value = NULL;
    g_hash_table_iter_init(&iter, source);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
      FocusGuardUsage *usage = value;
      if (usage == NULL || usage->usec_total <= 0) {
        continue;
      }
      g_ptr_array_add(entries, usage);
    }
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
    gtk_label_set_text(GTK_LABEL(guard->state->focus_stats_empty_label),
                       empty_text ? empty_text : "No app activity yet.");
    gtk_widget_set_visible(guard->state->focus_stats_empty_label, shown == 0);
  }

  guard->usage_dirty = FALSE;
  g_ptr_array_free(entries, TRUE);
}
