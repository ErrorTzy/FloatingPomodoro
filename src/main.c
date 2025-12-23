#include <gtk/gtk.h>
#include <fontconfig/fontconfig.h>
#include <pango/pangocairo.h>

#include "config.h"
#include "core/task_store.h"
#include "storage/task_storage.h"
#include "xfce4_floating_pomodoro_resources.h"

typedef enum {
  LOG_LEVEL_WARN = 0,
  LOG_LEVEL_INFO = 1,
  LOG_LEVEL_DEBUG = 2
} LogLevel;

static LogLevel log_level_threshold = LOG_LEVEL_WARN;

static LogLevel
parse_log_level(void)
{
  const char *value = g_getenv("POMODORO_LOG_LEVEL");

  if (value == NULL || *value == '\0') {
    return LOG_LEVEL_WARN;
  }

  if (g_ascii_strcasecmp(value, "debug") == 0) {
    return LOG_LEVEL_DEBUG;
  }

  if (g_ascii_strcasecmp(value, "info") == 0 ||
      g_ascii_strcasecmp(value, "message") == 0) {
    return LOG_LEVEL_INFO;
  }

  if (g_ascii_strcasecmp(value, "warn") == 0 ||
      g_ascii_strcasecmp(value, "warning") == 0 ||
      g_ascii_strcasecmp(value, "error") == 0) {
    return LOG_LEVEL_WARN;
  }

  g_warning("Unknown POMODORO_LOG_LEVEL='%s', defaulting to 'warn'", value);
  return LOG_LEVEL_WARN;
}

static gboolean
should_log(GLogLevelFlags level)
{
  if (level & (G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING)) {
    return TRUE;
  }

  if ((level & (G_LOG_LEVEL_MESSAGE | G_LOG_LEVEL_INFO)) != 0) {
    return log_level_threshold >= LOG_LEVEL_INFO;
  }

  if ((level & G_LOG_LEVEL_DEBUG) != 0) {
    return log_level_threshold >= LOG_LEVEL_DEBUG;
  }

  return FALSE;
}

static GLogWriterOutput
pomodoro_log_writer(GLogLevelFlags level,
                    const GLogField *fields,
                    gsize n_fields,
                    gpointer user_data)
{
  (void)user_data;

  if (!should_log(level)) {
    return G_LOG_WRITER_HANDLED;
  }

  return g_log_writer_default(level, fields, n_fields, user_data);
}

static void
install_logging(void)
{
  log_level_threshold = parse_log_level();
  g_log_set_writer_func(pomodoro_log_writer, NULL, NULL);
}

static void
install_resources(void)
{
  g_resources_register(xfce4_floating_pomodoro_get_resource());
}

static void
install_fonts(void)
{
  const char *font_resources[] = {
      "/com/scott/Xfce4FloatingPomodoro/fonts/Manrope-Regular.ttf",
      "/com/scott/Xfce4FloatingPomodoro/fonts/Manrope-SemiBold.ttf",
      "/com/scott/Xfce4FloatingPomodoro/fonts/Manrope-Bold.ttf"};
  const char *font_files[] = {"Manrope-Regular.ttf",
                              "Manrope-SemiBold.ttf",
                              "Manrope-Bold.ttf"};
  const size_t font_count = G_N_ELEMENTS(font_resources);

  char *font_dir = g_build_filename(g_get_user_cache_dir(),
                                    "xfce4-floating-pomodoro",
                                    "fonts",
                                    NULL);
  g_mkdir_with_parents(font_dir, 0755);

  for (size_t i = 0; i < font_count; i++) {
    char *target_path = g_build_filename(font_dir, font_files[i], NULL);
    if (!g_file_test(target_path, G_FILE_TEST_EXISTS)) {
      GError *error = NULL;
      GBytes *data = g_resources_lookup_data(font_resources[i],
                                             G_RESOURCE_LOOKUP_FLAGS_NONE,
                                             &error);
      if (data == NULL) {
        g_warning("Failed to load font resource '%s': %s",
                  font_resources[i],
                  error ? error->message : "unknown error");
        g_clear_error(&error);
      } else {
        gsize length = 0;
        const char *bytes = g_bytes_get_data(data, &length);
        if (!g_file_set_contents(target_path, bytes, length, &error)) {
          g_warning("Failed to write font file '%s': %s",
                    target_path,
                    error ? error->message : "unknown error");
          g_clear_error(&error);
        }
        g_bytes_unref(data);
      }
    }
    g_free(target_path);
  }

  FcConfig *config = FcInitLoadConfigAndFonts();
  if (config == NULL) {
    g_debug("Fontconfig not available; skipping bundled font registration");
    g_free(font_dir);
    return;
  }

  FcConfigSetCurrent(config);

  gboolean added = FcConfigAppFontAddDir(config,
                                         (const FcChar8 *)font_dir);
  if (!added) {
    g_debug("Failed to register bundled fonts from '%s'", font_dir);
    g_free(font_dir);
    return;
  }

  PangoFontMap *fontmap = pango_cairo_font_map_get_default();
  if (fontmap != NULL) {
    pango_font_map_changed(fontmap);
  }

  g_free(font_dir);
}

static void
load_css(void)
{
  GtkCssProvider *provider = gtk_css_provider_new();

  gtk_css_provider_load_from_resource(
      provider,
      "/com/scott/Xfce4FloatingPomodoro/styles/app.css");
  gtk_style_context_add_provider_for_display(
      gdk_display_get_default(),
      GTK_STYLE_PROVIDER(provider),
      GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  g_object_unref(provider);
}

typedef enum {
  TASK_ACTION_COMPLETE = 0,
  TASK_ACTION_REACTIVATE = 1,
  TASK_ACTION_EDIT = 2
} TaskAction;

typedef struct {
  TaskStore *store;
  GtkWindow *window;
  GtkWidget *task_list;
  GtkWidget *task_empty_label;
  GtkWidget *archived_list;
  GtkWidget *archived_empty_label;
  GtkWidget *task_entry;
  GtkWidget *current_task_label;
  GtkWidget *current_task_meta;
  GtkWidget *archive_dropdown;
  GtkWidget *archive_days_row;
  GtkWidget *archive_keep_row;
  GtkSpinButton *archive_days_spin;
  GtkSpinButton *archive_keep_spin;
  gboolean suppress_archive_signals;
} AppState;

typedef struct {
  AppState *state;
  PomodoroTask *task;
  GtkWidget *window;
  GtkWidget *entry;
} RenameDialog;

static void on_task_action_clicked(GtkButton *button, gpointer user_data);
static void on_archive_strategy_changed(GObject *object,
                                        GParamSpec *pspec,
                                        gpointer user_data);
static void on_archive_value_changed(GtkSpinButton *spin, gpointer user_data);
static void refresh_task_list(AppState *state);
static void save_task_store(AppState *state);

static void
app_state_free(gpointer data)
{
  AppState *state = data;
  if (state == NULL) {
    return;
  }

  task_store_free(state->store);
  g_free(state);
}

static void
rename_dialog_free(gpointer data)
{
  RenameDialog *dialog = data;
  if (dialog == NULL) {
    return;
  }

  g_free(dialog);
}

static void
apply_rename_dialog(RenameDialog *dialog)
{
  if (dialog == NULL || dialog->state == NULL || dialog->task == NULL) {
    return;
  }

  const char *text = gtk_editable_get_text(GTK_EDITABLE(dialog->entry));
  if (text != NULL) {
    pomodoro_task_set_title(dialog->task, text);
    task_store_apply_archive_policy(dialog->state->store);
    save_task_store(dialog->state);
    refresh_task_list(dialog->state);
  }

  gtk_window_destroy(GTK_WINDOW(dialog->window));
}

static void
on_rename_confirm_clicked(GtkButton *button, gpointer user_data)
{
  (void)button;
  apply_rename_dialog((RenameDialog *)user_data);
}

static void
on_rename_entry_activate(GtkEntry *entry, gpointer user_data)
{
  (void)entry;
  apply_rename_dialog((RenameDialog *)user_data);
}

static gboolean
on_rename_window_close(GtkWindow *window, gpointer user_data)
{
  (void)user_data;
  gtk_window_destroy(window);
  return TRUE;
}

static void
on_rename_cancel_clicked(GtkButton *button, gpointer user_data)
{
  (void)button;
  gtk_window_destroy(GTK_WINDOW(user_data));
}

static void
show_rename_dialog(AppState *state, PomodoroTask *task)
{
  if (state == NULL || task == NULL) {
    return;
  }

  GtkWidget *dialog = gtk_window_new();
  gtk_window_set_title(GTK_WINDOW(dialog), "Rename Task");
  gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
  gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);
  gtk_window_set_transient_for(GTK_WINDOW(dialog), state->window);
  gtk_window_set_default_size(GTK_WINDOW(dialog), 360, 140);

  GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_top(root, 16);
  gtk_widget_set_margin_bottom(root, 16);
  gtk_widget_set_margin_start(root, 16);
  gtk_widget_set_margin_end(root, 16);

  GtkWidget *label = gtk_label_new("Update the task name");
  gtk_widget_add_css_class(label, "card-title");
  gtk_widget_set_halign(label, GTK_ALIGN_START);

  GtkWidget *entry = gtk_entry_new();
  gtk_editable_set_text(GTK_EDITABLE(entry), pomodoro_task_get_title(task));
  gtk_widget_set_hexpand(entry, TRUE);
  gtk_widget_add_css_class(entry, "task-entry");

  GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_halign(actions, GTK_ALIGN_END);

  GtkWidget *cancel = gtk_button_new_with_label("Cancel");
  gtk_widget_add_css_class(cancel, "btn-secondary");
  gtk_widget_add_css_class(cancel, "btn-compact");

  GtkWidget *save = gtk_button_new_with_label("Save");
  gtk_widget_add_css_class(save, "btn-primary");
  gtk_widget_add_css_class(save, "btn-compact");

  gtk_box_append(GTK_BOX(actions), cancel);
  gtk_box_append(GTK_BOX(actions), save);

  gtk_box_append(GTK_BOX(root), label);
  gtk_box_append(GTK_BOX(root), entry);
  gtk_box_append(GTK_BOX(root), actions);

  gtk_window_set_child(GTK_WINDOW(dialog), root);

  RenameDialog *dialog_state = g_new0(RenameDialog, 1);
  dialog_state->state = state;
  dialog_state->task = task;
  dialog_state->window = dialog;
  dialog_state->entry = entry;

  g_object_set_data_full(G_OBJECT(dialog),
                         "rename-dialog",
                         dialog_state,
                         rename_dialog_free);

  g_signal_connect(cancel,
                   "clicked",
                   G_CALLBACK(on_rename_cancel_clicked),
                   dialog);
  g_signal_connect(save,
                   "clicked",
                   G_CALLBACK(on_rename_confirm_clicked),
                   dialog_state);
  g_signal_connect(entry,
                   "activate",
                   G_CALLBACK(on_rename_entry_activate),
                   dialog_state);
  g_signal_connect(dialog,
                   "close-request",
                   G_CALLBACK(on_rename_window_close),
                   NULL);

  gtk_window_present(GTK_WINDOW(dialog));
}

static void
save_task_store(AppState *state)
{
  if (state == NULL || state->store == NULL) {
    return;
  }

  GError *error = NULL;
  if (!task_storage_save(state->store, &error)) {
    g_warning("Failed to save tasks: %s", error ? error->message : "unknown error");
    g_clear_error(&error);
  }
}

static void
update_current_task_summary(AppState *state)
{
  if (state == NULL || state->current_task_label == NULL) {
    return;
  }

  const GPtrArray *tasks = task_store_get_tasks(state->store);
  const PomodoroTask *active_task = NULL;

  if (tasks != NULL) {
    for (guint i = 0; i < tasks->len; i++) {
      PomodoroTask *task = g_ptr_array_index((GPtrArray *)tasks, i);
      if (task != NULL && pomodoro_task_get_status(task) == TASK_STATUS_ACTIVE) {
        active_task = task;
        break;
      }
    }
  }

  if (active_task != NULL) {
    gtk_label_set_text(GTK_LABEL(state->current_task_label),
                       pomodoro_task_get_title(active_task));
    if (state->current_task_meta != NULL) {
      gtk_label_set_text(GTK_LABEL(state->current_task_meta),
                         "Ready for the next focus session");
    }
  } else {
    gtk_label_set_text(GTK_LABEL(state->current_task_label), "No active task");
    if (state->current_task_meta != NULL) {
      gtk_label_set_text(GTK_LABEL(state->current_task_meta),
                         "Add a task below or reactivate a completed one");
    }
  }
}

static void
append_task_row(AppState *state, GtkWidget *list, PomodoroTask *task)
{
  if (state == NULL || list == NULL || task == NULL) {
    return;
  }

  TaskStatus status = pomodoro_task_get_status(task);

  GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_widget_add_css_class(row, "task-row");

  GtkWidget *title = gtk_label_new(pomodoro_task_get_title(task));
  gtk_widget_add_css_class(title, "task-item");
  gtk_widget_set_halign(title, GTK_ALIGN_START);
  gtk_widget_set_hexpand(title, TRUE);
  gtk_label_set_wrap(GTK_LABEL(title), TRUE);
  gtk_label_set_xalign(GTK_LABEL(title), 0.0f);

  const char *status_text = "Active";
  if (status == TASK_STATUS_COMPLETED) {
    status_text = "Completed";
  } else if (status == TASK_STATUS_ARCHIVED) {
    status_text = "Archived";
  }

  GtkWidget *status_tag = gtk_label_new(status_text);
  gtk_widget_add_css_class(status_tag, "tag");
  if (status == TASK_STATUS_COMPLETED) {
    gtk_widget_add_css_class(status_tag, "tag-success");
  } else if (status == TASK_STATUS_ARCHIVED) {
    gtk_widget_add_css_class(status_tag, "tag-muted");
  }

  GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_widget_set_halign(actions, GTK_ALIGN_END);

  GtkWidget *edit_button = gtk_button_new_with_label("Edit");
  gtk_widget_add_css_class(edit_button, "btn-secondary");
  gtk_widget_add_css_class(edit_button, "btn-compact");
  g_object_set_data(G_OBJECT(edit_button), "task", task);
  g_object_set_data(G_OBJECT(edit_button),
                    "task-action",
                    GINT_TO_POINTER(TASK_ACTION_EDIT));
  g_signal_connect(edit_button,
                   "clicked",
                   G_CALLBACK(on_task_action_clicked),
                   state);

  GtkWidget *action_button = NULL;
  TaskAction action = TASK_ACTION_COMPLETE;
  if (status == TASK_STATUS_ACTIVE) {
    action_button = gtk_button_new_with_label("Complete");
    gtk_widget_add_css_class(action_button, "btn-secondary");
    action = TASK_ACTION_COMPLETE;
  } else if (status == TASK_STATUS_COMPLETED) {
    action_button = gtk_button_new_with_label("Reactivate");
    gtk_widget_add_css_class(action_button, "btn-primary");
    action = TASK_ACTION_REACTIVATE;
  } else if (status == TASK_STATUS_ARCHIVED) {
    action_button = gtk_button_new_with_label("Restore");
    gtk_widget_add_css_class(action_button, "btn-primary");
    action = TASK_ACTION_REACTIVATE;
  }

  if (action_button != NULL) {
    gtk_widget_add_css_class(action_button, "btn-compact");
    g_object_set_data(G_OBJECT(action_button), "task", task);
    g_object_set_data(G_OBJECT(action_button),
                      "task-action",
                      GINT_TO_POINTER(action));
    g_signal_connect(action_button,
                     "clicked",
                     G_CALLBACK(on_task_action_clicked),
                     state);
  }

  gtk_box_append(GTK_BOX(row), title);
  gtk_box_append(GTK_BOX(row), status_tag);
  gtk_box_append(GTK_BOX(row), actions);
  gtk_box_append(GTK_BOX(actions), edit_button);
  if (action_button != NULL) {
    gtk_box_append(GTK_BOX(actions), action_button);
  }

  gtk_list_box_append(GTK_LIST_BOX(list), row);
}

static void
refresh_task_list(AppState *state)
{
  if (state == NULL || state->task_list == NULL || state->archived_list == NULL) {
    return;
  }

  GtkWidget *row = gtk_widget_get_first_child(state->task_list);
  while (row != NULL) {
    GtkWidget *next = gtk_widget_get_next_sibling(row);
    gtk_list_box_remove(GTK_LIST_BOX(state->task_list), row);
    row = next;
  }

  row = gtk_widget_get_first_child(state->archived_list);
  while (row != NULL) {
    GtkWidget *next = gtk_widget_get_next_sibling(row);
    gtk_list_box_remove(GTK_LIST_BOX(state->archived_list), row);
    row = next;
  }

  const GPtrArray *tasks = task_store_get_tasks(state->store);
  guint visible_count = 0;
  guint archived_count = 0;

  if (tasks != NULL) {
    for (guint i = 0; i < tasks->len; i++) {
      PomodoroTask *task = g_ptr_array_index((GPtrArray *)tasks, i);
      if (task != NULL && pomodoro_task_get_status(task) == TASK_STATUS_ACTIVE) {
        append_task_row(state, state->task_list, task);
        visible_count++;
      }
    }

    for (guint i = 0; i < tasks->len; i++) {
      PomodoroTask *task = g_ptr_array_index((GPtrArray *)tasks, i);
      if (task != NULL &&
          pomodoro_task_get_status(task) == TASK_STATUS_COMPLETED) {
        append_task_row(state, state->task_list, task);
        visible_count++;
      }
    }

    for (guint i = 0; i < tasks->len; i++) {
      PomodoroTask *task = g_ptr_array_index((GPtrArray *)tasks, i);
      if (task != NULL &&
          pomodoro_task_get_status(task) == TASK_STATUS_ARCHIVED) {
        append_task_row(state, state->archived_list, task);
        archived_count++;
      }
    }
  }

  if (state->task_empty_label != NULL) {
    gtk_widget_set_visible(state->task_empty_label, visible_count == 0);
  }
  if (state->archived_empty_label != NULL) {
    gtk_widget_set_visible(state->archived_empty_label, archived_count == 0);
  }

  update_current_task_summary(state);
}

static void
update_archive_controls(AppState *state)
{
  if (state == NULL) {
    return;
  }

  state->suppress_archive_signals = TRUE;

  TaskArchiveStrategy strategy = task_store_get_archive_strategy(state->store);

  if (state->archive_dropdown != NULL) {
    guint selected = 0;
    switch (strategy.type) {
      case TASK_ARCHIVE_AFTER_DAYS:
        selected = 0;
        break;
      case TASK_ARCHIVE_IMMEDIATE:
        selected = 1;
        break;
      case TASK_ARCHIVE_KEEP_LATEST:
        selected = 2;
        break;
    }
    gtk_drop_down_set_selected(GTK_DROP_DOWN(state->archive_dropdown), selected);
  }

  if (state->archive_days_spin != NULL) {
    gtk_spin_button_set_value(state->archive_days_spin, (gdouble)strategy.days);
  }
  if (state->archive_keep_spin != NULL) {
    gtk_spin_button_set_value(state->archive_keep_spin,
                              (gdouble)strategy.keep_latest);
  }

  if (state->archive_days_row != NULL) {
    gtk_widget_set_visible(state->archive_days_row,
                           strategy.type == TASK_ARCHIVE_AFTER_DAYS);
  }
  if (state->archive_keep_row != NULL) {
    gtk_widget_set_visible(state->archive_keep_row,
                           strategy.type == TASK_ARCHIVE_KEEP_LATEST);
  }

  state->suppress_archive_signals = FALSE;
}

static void
apply_archive_settings(AppState *state)
{
  if (state == NULL || state->store == NULL) {
    return;
  }

  TaskArchiveStrategy strategy = task_store_get_archive_strategy(state->store);

  if (state->archive_dropdown != NULL) {
    guint selected =
        gtk_drop_down_get_selected(GTK_DROP_DOWN(state->archive_dropdown));
    if (selected == 1) {
      strategy.type = TASK_ARCHIVE_IMMEDIATE;
    } else if (selected == 2) {
      strategy.type = TASK_ARCHIVE_KEEP_LATEST;
    } else {
      strategy.type = TASK_ARCHIVE_AFTER_DAYS;
    }
  }

  if (state->archive_days_spin != NULL) {
    strategy.days = (guint)gtk_spin_button_get_value_as_int(
        state->archive_days_spin);
  }
  if (state->archive_keep_spin != NULL) {
    strategy.keep_latest = (guint)gtk_spin_button_get_value_as_int(
        state->archive_keep_spin);
  }

  task_store_set_archive_strategy(state->store, strategy);
  task_store_apply_archive_policy(state->store);
  save_task_store(state);
  refresh_task_list(state);
  update_archive_controls(state);
}

static void
handle_add_task(AppState *state)
{
  if (state == NULL || state->task_entry == NULL) {
    return;
  }

  const char *text =
      gtk_editable_get_text(GTK_EDITABLE(state->task_entry));
  if (text == NULL) {
    return;
  }

  char *trimmed = g_strstrip(g_strdup(text));
  if (trimmed[0] == '\0') {
    g_free(trimmed);
    return;
  }

  task_store_add(state->store, trimmed);
  task_store_apply_archive_policy(state->store);
  save_task_store(state);

  gtk_editable_set_text(GTK_EDITABLE(state->task_entry), "");

  refresh_task_list(state);
  g_free(trimmed);
}

static void
on_add_task_clicked(GtkButton *button, gpointer user_data)
{
  (void)button;
  handle_add_task((AppState *)user_data);
}

static void
on_task_entry_activate(GtkEntry *entry, gpointer user_data)
{
  (void)entry;
  handle_add_task((AppState *)user_data);
}

static void
on_task_action_clicked(GtkButton *button, gpointer user_data)
{
  AppState *state = user_data;
  if (state == NULL || button == NULL) {
    return;
  }

  PomodoroTask *task = g_object_get_data(G_OBJECT(button), "task");
  TaskAction action =
      GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "task-action"));

  if (task == NULL) {
    return;
  }

  if (action == TASK_ACTION_EDIT) {
    show_rename_dialog(state, task);
    return;
  }

  if (action == TASK_ACTION_REACTIVATE) {
    task_store_reactivate(state->store, task);
  } else {
    task_store_complete(state->store, task);
  }

  task_store_apply_archive_policy(state->store);
  save_task_store(state);
  refresh_task_list(state);
}

static void
on_archive_strategy_changed(GObject *object, GParamSpec *pspec, gpointer user_data)
{
  (void)object;
  (void)pspec;

  AppState *state = user_data;
  if (state == NULL || state->suppress_archive_signals) {
    return;
  }

  apply_archive_settings(state);
}

static void
on_archive_value_changed(GtkSpinButton *spin, gpointer user_data)
{
  (void)spin;

  AppState *state = user_data;
  if (state == NULL || state->suppress_archive_signals) {
    return;
  }

  apply_archive_settings(state);
}

static void
on_startup(GApplication *app, gpointer user_data)
{
  (void)app;
  (void)user_data;

  install_fonts();
  load_css();
}

static void
on_activate(GtkApplication *app, gpointer user_data)
{
  (void)user_data;

  GtkWidget *window = gtk_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(window), APP_NAME);
  gtk_window_set_default_size(GTK_WINDOW(window), 880, 560);
  gtk_widget_add_css_class(window, "app-window");

  TaskStore *store = task_store_new();
  GError *error = NULL;
  if (!task_storage_load(store, &error)) {
    g_warning("Failed to load tasks: %s", error ? error->message : "unknown error");
    g_clear_error(&error);
  }
  task_store_apply_archive_policy(store);

  AppState *state = g_new0(AppState, 1);
  state->store = store;
  state->window = GTK_WINDOW(window);
  g_object_set_data_full(G_OBJECT(window), "app-state", state, app_state_free);

  GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
  gtk_widget_add_css_class(root, "app-root");

  GtkWidget *header = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
  gtk_widget_add_css_class(header, "app-header");

  GtkWidget *title = gtk_label_new(APP_NAME);
  gtk_widget_add_css_class(title, "app-title");
  gtk_widget_set_halign(title, GTK_ALIGN_START);

  GtkWidget *subtitle = gtk_label_new("Task persistence is live. Timer controls arrive next.");
  gtk_widget_add_css_class(subtitle, "app-subtitle");
  gtk_widget_set_halign(subtitle, GTK_ALIGN_START);
  gtk_label_set_wrap(GTK_LABEL(subtitle), TRUE);

  gtk_box_append(GTK_BOX(header), title);
  gtk_box_append(GTK_BOX(header), subtitle);
  gtk_box_append(GTK_BOX(root), header);

  GtkWidget *hero = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 18);
  gtk_widget_set_hexpand(hero, TRUE);

  GtkWidget *timer_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_add_css_class(timer_card, "card");
  gtk_widget_set_hexpand(timer_card, TRUE);

  GtkWidget *timer_title = gtk_label_new("Focus Session");
  gtk_widget_add_css_class(timer_title, "card-title");
  gtk_widget_set_halign(timer_title, GTK_ALIGN_START);

  GtkWidget *timer_value = gtk_label_new("25:00");
  gtk_widget_add_css_class(timer_value, "timer-value");
  gtk_widget_set_halign(timer_value, GTK_ALIGN_START);

  GtkWidget *timer_pill = gtk_label_new("Next: short break");
  gtk_widget_add_css_class(timer_pill, "pill");
  gtk_widget_set_halign(timer_pill, GTK_ALIGN_START);

  GtkWidget *timer_actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  gtk_widget_set_halign(timer_actions, GTK_ALIGN_START);

  GtkWidget *start_button = gtk_button_new_with_label("Start Focus");
  gtk_widget_add_css_class(start_button, "btn-primary");

  GtkWidget *skip_button = gtk_button_new_with_label("Skip");
  gtk_widget_add_css_class(skip_button, "btn-secondary");

  gtk_box_append(GTK_BOX(timer_actions), start_button);
  gtk_box_append(GTK_BOX(timer_actions), skip_button);

  gtk_box_append(GTK_BOX(timer_card), timer_title);
  gtk_box_append(GTK_BOX(timer_card), timer_value);
  gtk_box_append(GTK_BOX(timer_card), timer_pill);
  gtk_box_append(GTK_BOX(timer_card), timer_actions);

  GtkWidget *task_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_widget_add_css_class(task_card, "card");
  gtk_widget_set_hexpand(task_card, TRUE);

  GtkWidget *task_title = gtk_label_new("Current Task");
  gtk_widget_add_css_class(task_title, "card-title");
  gtk_widget_set_halign(task_title, GTK_ALIGN_START);

  GtkWidget *task_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_halign(task_row, GTK_ALIGN_START);

  GtkWidget *task_label = gtk_label_new("No active task");
  gtk_widget_add_css_class(task_label, "task-item");
  state->current_task_label = task_label;

  GtkWidget *task_tag = gtk_label_new("Ready");
  gtk_widget_add_css_class(task_tag, "tag");

  gtk_box_append(GTK_BOX(task_row), task_label);
  gtk_box_append(GTK_BOX(task_row), task_tag);

  GtkWidget *task_meta = gtk_label_new("Add a task below or reactivate a completed one");
  gtk_widget_add_css_class(task_meta, "task-meta");
  gtk_widget_set_halign(task_meta, GTK_ALIGN_START);
  state->current_task_meta = task_meta;

  GtkWidget *stats_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 18);
  gtk_widget_set_halign(stats_row, GTK_ALIGN_START);

  GtkWidget *stat_block_left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  GtkWidget *stat_value_left = gtk_label_new("00:00");
  gtk_widget_add_css_class(stat_value_left, "stat-value");
  GtkWidget *stat_label_left = gtk_label_new("Focus time");
  gtk_widget_add_css_class(stat_label_left, "stat-label");
  gtk_box_append(GTK_BOX(stat_block_left), stat_value_left);
  gtk_box_append(GTK_BOX(stat_block_left), stat_label_left);

  GtkWidget *stat_block_right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  GtkWidget *stat_value_right = gtk_label_new("0");
  gtk_widget_add_css_class(stat_value_right, "stat-value");
  GtkWidget *stat_label_right = gtk_label_new("Breaks");
  gtk_widget_add_css_class(stat_label_right, "stat-label");
  gtk_box_append(GTK_BOX(stat_block_right), stat_value_right);
  gtk_box_append(GTK_BOX(stat_block_right), stat_label_right);

  gtk_box_append(GTK_BOX(stats_row), stat_block_left);
  gtk_box_append(GTK_BOX(stats_row), stat_block_right);

  gtk_box_append(GTK_BOX(task_card), task_title);
  gtk_box_append(GTK_BOX(task_card), task_row);
  gtk_box_append(GTK_BOX(task_card), task_meta);
  gtk_box_append(GTK_BOX(task_card), stats_row);

  gtk_box_append(GTK_BOX(hero), timer_card);
  gtk_box_append(GTK_BOX(hero), task_card);
  gtk_box_append(GTK_BOX(root), hero);

  GtkWidget *task_section = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 18);
  gtk_widget_set_hexpand(task_section, TRUE);

  GtkWidget *tasks_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_add_css_class(tasks_card, "card");
  gtk_widget_set_hexpand(tasks_card, TRUE);

  GtkWidget *tasks_title = gtk_label_new("Tasks");
  gtk_widget_add_css_class(tasks_title, "card-title");
  gtk_widget_set_halign(tasks_title, GTK_ALIGN_START);

  GtkWidget *task_input_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  gtk_widget_set_hexpand(task_input_row, TRUE);

  GtkWidget *task_entry = gtk_entry_new();
  gtk_widget_set_hexpand(task_entry, TRUE);
  gtk_entry_set_placeholder_text(GTK_ENTRY(task_entry),
                                 "Add a task for the next focus block");
  gtk_widget_add_css_class(task_entry, "task-entry");
  g_signal_connect(task_entry,
                   "activate",
                   G_CALLBACK(on_task_entry_activate),
                   state);
  state->task_entry = task_entry;

  GtkWidget *task_add_button = gtk_button_new_with_label("Add");
  gtk_widget_add_css_class(task_add_button, "btn-primary");
  gtk_widget_add_css_class(task_add_button, "btn-compact");
  g_signal_connect(task_add_button,
                   "clicked",
                   G_CALLBACK(on_add_task_clicked),
                   state);

  gtk_box_append(GTK_BOX(task_input_row), task_entry);
  gtk_box_append(GTK_BOX(task_input_row), task_add_button);

  GtkWidget *task_list = gtk_list_box_new();
  gtk_widget_add_css_class(task_list, "task-list");
  gtk_list_box_set_selection_mode(GTK_LIST_BOX(task_list), GTK_SELECTION_NONE);
  state->task_list = task_list;

  GtkWidget *task_scroller = gtk_scrolled_window_new();
  gtk_widget_add_css_class(task_scroller, "task-scroller");
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(task_scroller),
                                 GTK_POLICY_NEVER,
                                 GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(task_scroller), task_list);
  gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(task_scroller),
                                             220);
  gtk_widget_set_vexpand(task_scroller, TRUE);

  GtkWidget *task_empty_label =
      gtk_label_new("No tasks yet. Add one to start tracking your focus.");
  gtk_widget_add_css_class(task_empty_label, "task-empty");
  gtk_widget_set_halign(task_empty_label, GTK_ALIGN_START);
  gtk_label_set_wrap(GTK_LABEL(task_empty_label), TRUE);
  state->task_empty_label = task_empty_label;

  GtkWidget *archived_expander = gtk_expander_new("Archived tasks");
  gtk_widget_add_css_class(archived_expander, "archive-expander");
  gtk_expander_set_expanded(GTK_EXPANDER(archived_expander), FALSE);

  GtkWidget *archived_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);

  GtkWidget *archived_list = gtk_list_box_new();
  gtk_widget_add_css_class(archived_list, "task-list");
  gtk_list_box_set_selection_mode(GTK_LIST_BOX(archived_list),
                                  GTK_SELECTION_NONE);
  state->archived_list = archived_list;

  GtkWidget *archived_scroller = gtk_scrolled_window_new();
  gtk_widget_add_css_class(archived_scroller, "task-scroller");
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(archived_scroller),
                                 GTK_POLICY_NEVER,
                                 GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(archived_scroller),
                                archived_list);
  gtk_scrolled_window_set_min_content_height(
      GTK_SCROLLED_WINDOW(archived_scroller),
      160);

  GtkWidget *archived_empty_label =
      gtk_label_new("No archived tasks yet.");
  gtk_widget_add_css_class(archived_empty_label, "task-empty");
  gtk_widget_set_halign(archived_empty_label, GTK_ALIGN_START);
  gtk_label_set_wrap(GTK_LABEL(archived_empty_label), TRUE);
  state->archived_empty_label = archived_empty_label;

  gtk_box_append(GTK_BOX(archived_box), archived_scroller);
  gtk_box_append(GTK_BOX(archived_box), archived_empty_label);
  gtk_expander_set_child(GTK_EXPANDER(archived_expander), archived_box);

  gtk_box_append(GTK_BOX(tasks_card), tasks_title);
  gtk_box_append(GTK_BOX(tasks_card), task_input_row);
  gtk_box_append(GTK_BOX(tasks_card), task_scroller);
  gtk_box_append(GTK_BOX(tasks_card), task_empty_label);
  gtk_box_append(GTK_BOX(tasks_card), archived_expander);

  GtkWidget *settings_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_add_css_class(settings_card, "card");

  GtkWidget *settings_title = gtk_label_new("Archive Rules");
  gtk_widget_add_css_class(settings_title, "card-title");
  gtk_widget_set_halign(settings_title, GTK_ALIGN_START);

  GtkWidget *settings_desc =
      gtk_label_new("Completed tasks archive automatically to keep the list tidy.");
  gtk_widget_add_css_class(settings_desc, "task-meta");
  gtk_widget_set_halign(settings_desc, GTK_ALIGN_START);
  gtk_label_set_wrap(GTK_LABEL(settings_desc), TRUE);

  GtkStringList *archive_options = gtk_string_list_new(NULL);
  gtk_string_list_append(archive_options, "Archive after N days");
  gtk_string_list_append(archive_options, "Archive immediately");
  gtk_string_list_append(archive_options, "Keep latest N completed");

  GtkWidget *archive_dropdown =
      gtk_drop_down_new(G_LIST_MODEL(archive_options), NULL);
  g_object_unref(archive_options);
  gtk_widget_add_css_class(archive_dropdown, "archive-dropdown");
  gtk_widget_set_hexpand(archive_dropdown, TRUE);
  g_signal_connect(archive_dropdown,
                   "notify::selected",
                   G_CALLBACK(on_archive_strategy_changed),
                   state);
  state->archive_dropdown = archive_dropdown;

  GtkWidget *archive_days_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  GtkWidget *archive_days_label = gtk_label_new("Days to keep");
  gtk_widget_add_css_class(archive_days_label, "setting-label");
  gtk_widget_set_halign(archive_days_label, GTK_ALIGN_START);
  gtk_widget_set_hexpand(archive_days_label, TRUE);
  GtkWidget *archive_days_spin =
      gtk_spin_button_new_with_range(1, 90, 1);
  gtk_widget_set_halign(archive_days_spin, GTK_ALIGN_END);
  g_signal_connect(archive_days_spin,
                   "value-changed",
                   G_CALLBACK(on_archive_value_changed),
                   state);
  state->archive_days_spin = GTK_SPIN_BUTTON(archive_days_spin);
  state->archive_days_row = archive_days_row;

  gtk_box_append(GTK_BOX(archive_days_row), archive_days_label);
  gtk_box_append(GTK_BOX(archive_days_row), archive_days_spin);

  GtkWidget *archive_keep_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  GtkWidget *archive_keep_label = gtk_label_new("Keep latest");
  gtk_widget_add_css_class(archive_keep_label, "setting-label");
  gtk_widget_set_halign(archive_keep_label, GTK_ALIGN_START);
  gtk_widget_set_hexpand(archive_keep_label, TRUE);
  GtkWidget *archive_keep_spin =
      gtk_spin_button_new_with_range(1, 50, 1);
  gtk_widget_set_halign(archive_keep_spin, GTK_ALIGN_END);
  g_signal_connect(archive_keep_spin,
                   "value-changed",
                   G_CALLBACK(on_archive_value_changed),
                   state);
  state->archive_keep_spin = GTK_SPIN_BUTTON(archive_keep_spin);
  state->archive_keep_row = archive_keep_row;

  gtk_box_append(GTK_BOX(archive_keep_row), archive_keep_label);
  gtk_box_append(GTK_BOX(archive_keep_row), archive_keep_spin);

  GtkWidget *archive_hint =
      gtk_label_new("Changes apply immediately and can be adjusted anytime.");
  gtk_widget_add_css_class(archive_hint, "task-meta");
  gtk_widget_set_halign(archive_hint, GTK_ALIGN_START);
  gtk_label_set_wrap(GTK_LABEL(archive_hint), TRUE);

  gtk_box_append(GTK_BOX(settings_card), settings_title);
  gtk_box_append(GTK_BOX(settings_card), settings_desc);
  gtk_box_append(GTK_BOX(settings_card), archive_dropdown);
  gtk_box_append(GTK_BOX(settings_card), archive_days_row);
  gtk_box_append(GTK_BOX(settings_card), archive_keep_row);
  gtk_box_append(GTK_BOX(settings_card), archive_hint);

  gtk_box_append(GTK_BOX(task_section), tasks_card);
  gtk_box_append(GTK_BOX(task_section), settings_card);
  gtk_box_append(GTK_BOX(root), task_section);

  gtk_window_set_child(GTK_WINDOW(window), root);
  gtk_window_present(GTK_WINDOW(window));

  update_archive_controls(state);
  refresh_task_list(state);

  g_info("Main window presented");
}

int
main(int argc, char *argv[])
{
  install_logging();
  install_resources();

  g_set_application_name(APP_NAME);

  GtkApplication *app = gtk_application_new(APP_ID, G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect(app, "startup", G_CALLBACK(on_startup), NULL);
  g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);

  g_info("Starting %s", APP_NAME);

  int status = g_application_run(G_APPLICATION(app), argc, argv);

  g_object_unref(app);

  return status;
}
