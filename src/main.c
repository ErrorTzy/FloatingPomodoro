#include <gtk/gtk.h>
#include <fontconfig/fontconfig.h>
#include <pango/pangocairo.h>
#include <execinfo.h>
#include <signal.h>
#include <unistd.h>

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
  if (log_level_threshold >= LOG_LEVEL_INFO &&
      g_getenv("G_MESSAGES_DEBUG") == NULL) {
    g_setenv("G_MESSAGES_DEBUG", "all", FALSE);
  }
  g_log_set_writer_func(pomodoro_log_writer, NULL, NULL);
}

static void
crash_handler(int signum)
{
  void *frames[48];
  int size = backtrace(frames, (int)(sizeof(frames) / sizeof(frames[0])));
  g_printerr("Fatal signal %d received\n", signum);
  backtrace_symbols_fd(frames, size, STDERR_FILENO);
  _exit(1);
}

static void
install_crash_handler(void)
{
  signal(SIGSEGV, crash_handler);
  signal(SIGABRT, crash_handler);
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

typedef struct {
  TaskStore *store;
  GtkWindow *window;
  GtkWindow *settings_window;
  GtkWindow *archived_window;
  struct _TaskRowControls *editing_controls;
  GtkWidget *task_list;
  GtkWidget *task_empty_label;
  GtkWidget *task_entry;
  GtkWidget *task_repeat_spin;
  GtkWidget *task_repeat_hint;
  GtkWidget *current_task_label;
  GtkWidget *current_task_meta;
} AppState;

typedef struct {
  AppState *state;
  PomodoroTask *task;
  PomodoroTask *active_task;
  GtkWidget *window;
  gboolean switch_active;
} ConfirmDialog;

typedef struct _TaskRowControls {
  AppState *state;
  PomodoroTask *task;
  GtkWidget *repeat_label;
  GtkWidget *count_label;
  GtkWidget *title_label;
  GtkWidget *title_entry;
  GtkWidget *edit_button;
  gboolean title_edit_active;
  gboolean title_edit_has_focus;
  gint64 title_edit_started_at;
} TaskRowControls;

typedef struct {
  AppState *state;
  GtkWindow *window;
  GtkWidget *dropdown;
  GtkWidget *days_row;
  GtkWidget *keep_row;
  GtkSpinButton *days_spin;
  GtkSpinButton *keep_spin;
  gboolean suppress_signals;
} SettingsDialog;

typedef struct {
  GtkWindow *window;
  GtkWidget *list;
  GtkWidget *empty_label;
} ArchivedDialog;

static void on_show_settings_clicked(GtkButton *button, gpointer user_data);
static void on_show_archived_clicked(GtkButton *button, gpointer user_data);
static void on_settings_strategy_changed(GObject *object,
                                         GParamSpec *pspec,
                                         gpointer user_data);
static void on_settings_value_changed(GtkSpinButton *spin, gpointer user_data);
static void settings_dialog_update_controls(SettingsDialog *dialog);
static ArchivedDialog *archived_dialog_get(AppState *state);
static void refresh_task_list(AppState *state);
static void save_task_store(AppState *state);
static void update_repeat_hint(GtkSpinButton *spin, GtkWidget *label);
static void on_repeat_spin_changed(GtkSpinButton *spin, gpointer user_data);
static PomodoroTask *find_active_task(TaskStore *store);
static void on_task_status_clicked(GtkButton *button, gpointer user_data);
static void on_task_edit_clicked(GtkButton *button, gpointer user_data);
static void on_task_archive_clicked(GtkButton *button, gpointer user_data);
static void on_task_restore_clicked(GtkButton *button, gpointer user_data);
static void on_task_delete_clicked(GtkButton *button, gpointer user_data);
static void on_window_pressed(GtkGestureClick *gesture,
                              gint n_press,
                              gdouble x,
                              gdouble y,
                              gpointer user_data);
static void update_current_task_summary(AppState *state);
static void show_confirm_dialog(AppState *state,
                                const char *title_text,
                                const char *body_text,
                                PomodoroTask *task,
                                PomodoroTask *active_task,
                                gboolean switch_active);

static void
app_state_free(gpointer data)
{
  AppState *state = data;
  if (state == NULL) {
    return;
  }

  if (state->settings_window != NULL) {
    SettingsDialog *dialog =
        g_object_get_data(G_OBJECT(state->settings_window), "settings-dialog");
    if (dialog != NULL) {
      dialog->state = NULL;
    }
    gtk_window_destroy(state->settings_window);
  }

  if (state->archived_window != NULL) {
    ArchivedDialog *dialog = archived_dialog_get(state);
    if (dialog != NULL) {
      dialog->window = NULL;
      dialog->list = NULL;
      dialog->empty_label = NULL;
    }
    gtk_window_destroy(state->archived_window);
  }

  task_store_free(state->store);
  g_free(state);
}

static void
settings_dialog_free(gpointer data)
{
  SettingsDialog *dialog = data;
  if (dialog == NULL) {
    return;
  }

  g_free(dialog);
}

static void
archived_dialog_free(gpointer data)
{
  ArchivedDialog *dialog = data;
  if (dialog == NULL) {
    return;
  }

  g_free(dialog);
}

static void
confirm_dialog_free(gpointer data)
{
  ConfirmDialog *dialog = data;
  if (dialog == NULL) {
    return;
  }

  g_free(dialog);
}

static void
task_row_controls_free(gpointer data)
{
  TaskRowControls *controls = data;
  if (controls == NULL) {
    return;
  }

  g_free(controls);
}

static void
apply_confirm_dialog(ConfirmDialog *dialog)
{
  if (dialog == NULL || dialog->state == NULL || dialog->state->store == NULL ||
      dialog->task == NULL) {
    return;
  }

  if (dialog->switch_active) {
    if (dialog->active_task != NULL && dialog->active_task != dialog->task) {
      task_store_complete(dialog->state->store, dialog->active_task);
    }
    task_store_reactivate(dialog->state->store, dialog->task);
  } else {
    task_store_complete(dialog->state->store, dialog->task);
  }

  task_store_apply_archive_policy(dialog->state->store);
  save_task_store(dialog->state);
  refresh_task_list(dialog->state);

  gtk_window_destroy(GTK_WINDOW(dialog->window));
}

static void
on_confirm_ok_clicked(GtkButton *button, gpointer user_data)
{
  (void)button;
  apply_confirm_dialog((ConfirmDialog *)user_data);
}

static void
on_confirm_cancel_clicked(GtkButton *button, gpointer user_data)
{
  (void)button;
  gtk_window_destroy(GTK_WINDOW(user_data));
}

static void
show_confirm_dialog(AppState *state,
                    const char *title_text,
                    const char *body_text,
                    PomodoroTask *task,
                    PomodoroTask *active_task,
                    gboolean switch_active)
{
  if (state == NULL || task == NULL) {
    return;
  }

  GtkWidget *dialog = gtk_window_new();
  gtk_window_set_title(GTK_WINDOW(dialog), title_text);
  gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
  gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);
  gtk_window_set_transient_for(GTK_WINDOW(dialog), state->window);
  gtk_window_set_default_size(GTK_WINDOW(dialog), 420, 180);

  GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_top(root, 16);
  gtk_widget_set_margin_bottom(root, 16);
  gtk_widget_set_margin_start(root, 16);
  gtk_widget_set_margin_end(root, 16);

  GtkWidget *title = gtk_label_new(title_text);
  gtk_widget_add_css_class(title, "card-title");
  gtk_widget_set_halign(title, GTK_ALIGN_START);

  GtkWidget *body = gtk_label_new(body_text);
  gtk_widget_add_css_class(body, "task-meta");
  gtk_widget_set_halign(body, GTK_ALIGN_START);
  gtk_label_set_wrap(GTK_LABEL(body), TRUE);

  GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_halign(actions, GTK_ALIGN_END);

  GtkWidget *cancel = gtk_button_new_with_label("Cancel");
  gtk_widget_add_css_class(cancel, "btn-secondary");
  gtk_widget_add_css_class(cancel, "btn-compact");

  GtkWidget *confirm = gtk_button_new_with_label("Confirm");
  gtk_widget_add_css_class(confirm, "btn-primary");
  gtk_widget_add_css_class(confirm, "btn-compact");

  gtk_box_append(GTK_BOX(actions), cancel);
  gtk_box_append(GTK_BOX(actions), confirm);

  gtk_box_append(GTK_BOX(root), title);
  gtk_box_append(GTK_BOX(root), body);
  gtk_box_append(GTK_BOX(root), actions);

  gtk_window_set_child(GTK_WINDOW(dialog), root);

  ConfirmDialog *dialog_state = g_new0(ConfirmDialog, 1);
  dialog_state->state = state;
  dialog_state->task = task;
  dialog_state->active_task = active_task;
  dialog_state->window = dialog;
  dialog_state->switch_active = switch_active;

  g_object_set_data_full(G_OBJECT(dialog),
                         "confirm-dialog",
                         dialog_state,
                         confirm_dialog_free);

  g_signal_connect(cancel,
                   "clicked",
                   G_CALLBACK(on_confirm_cancel_clicked),
                   dialog);
  g_signal_connect(confirm,
                   "clicked",
                   G_CALLBACK(on_confirm_ok_clicked),
                   dialog_state);

  gtk_window_present(GTK_WINDOW(dialog));
}

static void
on_settings_window_destroy(GtkWidget *widget, gpointer user_data)
{
  (void)widget;
  SettingsDialog *dialog = user_data;
  if (dialog == NULL) {
    return;
  }

  dialog->suppress_signals = TRUE;
  g_info("Settings window destroyed");
  if (dialog->state != NULL) {
    dialog->state->settings_window = NULL;
  }
}

static gboolean
on_settings_window_close(GtkWindow *window, gpointer user_data)
{
  (void)window;
  SettingsDialog *dialog = user_data;
  if (dialog != NULL) {
    dialog->suppress_signals = TRUE;
    g_info("Settings window close requested");
  }
  return FALSE;
}

static void
on_archived_window_destroy(GtkWidget *widget, gpointer user_data)
{
  (void)widget;
  AppState *state = user_data;
  if (state == NULL) {
    return;
  }

  g_info("Archived window destroyed");
  state->archived_window = NULL;
}

static void
show_settings_window(AppState *state)
{
  if (state == NULL) {
    return;
  }

  if (state->settings_window != NULL) {
    gtk_window_present(state->settings_window);
    return;
  }

  GtkApplication *app = gtk_window_get_application(state->window);
  GtkWidget *window = gtk_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(window), "Archive Settings");
  gtk_window_set_transient_for(GTK_WINDOW(window), state->window);
  gtk_window_set_modal(GTK_WINDOW(window), TRUE);
  gtk_window_set_default_size(GTK_WINDOW(window), 420, 320);

  GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_top(root, 18);
  gtk_widget_set_margin_bottom(root, 18);
  gtk_widget_set_margin_start(root, 18);
  gtk_widget_set_margin_end(root, 18);

  GtkWidget *title = gtk_label_new("Archive rules");
  gtk_widget_add_css_class(title, "card-title");
  gtk_widget_set_halign(title, GTK_ALIGN_START);

  GtkWidget *desc =
      gtk_label_new("Completed tasks archive automatically to keep the list tidy.");
  gtk_widget_add_css_class(desc, "task-meta");
  gtk_widget_set_halign(desc, GTK_ALIGN_START);
  gtk_label_set_wrap(GTK_LABEL(desc), TRUE);

  const char *archive_options[] = {
      "Archive after N days",
      "Archive immediately",
      "Keep latest N completed",
      NULL};

  GtkWidget *archive_dropdown = gtk_drop_down_new_from_strings(archive_options);
  gtk_widget_add_css_class(archive_dropdown, "archive-dropdown");
  gtk_widget_set_hexpand(archive_dropdown, TRUE);

  GtkWidget *archive_days_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  GtkWidget *archive_days_label = gtk_label_new("Days to keep");
  gtk_widget_add_css_class(archive_days_label, "setting-label");
  gtk_widget_set_halign(archive_days_label, GTK_ALIGN_START);
  gtk_widget_set_hexpand(archive_days_label, TRUE);
  GtkWidget *archive_days_spin = gtk_spin_button_new_with_range(1, 90, 1);
  gtk_widget_set_halign(archive_days_spin, GTK_ALIGN_END);

  gtk_box_append(GTK_BOX(archive_days_row), archive_days_label);
  gtk_box_append(GTK_BOX(archive_days_row), archive_days_spin);

  GtkWidget *archive_keep_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  GtkWidget *archive_keep_label = gtk_label_new("Keep latest");
  gtk_widget_add_css_class(archive_keep_label, "setting-label");
  gtk_widget_set_halign(archive_keep_label, GTK_ALIGN_START);
  gtk_widget_set_hexpand(archive_keep_label, TRUE);
  GtkWidget *archive_keep_spin = gtk_spin_button_new_with_range(1, 50, 1);
  gtk_widget_set_halign(archive_keep_spin, GTK_ALIGN_END);

  gtk_box_append(GTK_BOX(archive_keep_row), archive_keep_label);
  gtk_box_append(GTK_BOX(archive_keep_row), archive_keep_spin);

  GtkWidget *hint =
      gtk_label_new("Changes apply immediately and can be adjusted anytime.");
  gtk_widget_add_css_class(hint, "task-meta");
  gtk_widget_set_halign(hint, GTK_ALIGN_START);
  gtk_label_set_wrap(GTK_LABEL(hint), TRUE);

  gtk_box_append(GTK_BOX(root), title);
  gtk_box_append(GTK_BOX(root), desc);
  gtk_box_append(GTK_BOX(root), archive_dropdown);
  gtk_box_append(GTK_BOX(root), archive_days_row);
  gtk_box_append(GTK_BOX(root), archive_keep_row);
  gtk_box_append(GTK_BOX(root), hint);

  gtk_window_set_child(GTK_WINDOW(window), root);
  state->settings_window = GTK_WINDOW(window);

  SettingsDialog *dialog = g_new0(SettingsDialog, 1);
  dialog->state = state;
  dialog->window = GTK_WINDOW(window);
  dialog->dropdown = archive_dropdown;
  dialog->days_row = archive_days_row;
  dialog->keep_row = archive_keep_row;
  dialog->days_spin = GTK_SPIN_BUTTON(archive_days_spin);
  dialog->keep_spin = GTK_SPIN_BUTTON(archive_keep_spin);

  g_signal_connect(archive_dropdown,
                   "notify::selected",
                   G_CALLBACK(on_settings_strategy_changed),
                   dialog);
  g_signal_connect(archive_days_spin,
                   "value-changed",
                   G_CALLBACK(on_settings_value_changed),
                   dialog);
  g_signal_connect(archive_keep_spin,
                   "value-changed",
                   G_CALLBACK(on_settings_value_changed),
                   dialog);

  g_object_set_data_full(G_OBJECT(window),
                         "settings-dialog",
                         dialog,
                         settings_dialog_free);

  g_signal_connect(window,
                   "close-request",
                   G_CALLBACK(on_settings_window_close),
                   dialog);
  g_signal_connect(window,
                   "destroy",
                   G_CALLBACK(on_settings_window_destroy),
                   dialog);

  settings_dialog_update_controls(dialog);
  gtk_window_present(GTK_WINDOW(window));
}

static void
show_archived_window(AppState *state)
{
  if (state == NULL) {
    return;
  }

  if (state->archived_window != NULL) {
    gtk_window_present(state->archived_window);
    return;
  }

  GtkApplication *app = gtk_window_get_application(state->window);
  GtkWidget *window = gtk_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(window), "Archived Tasks");
  gtk_window_set_transient_for(GTK_WINDOW(window), state->window);
  gtk_window_set_default_size(GTK_WINDOW(window), 520, 420);

  GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_top(root, 18);
  gtk_widget_set_margin_bottom(root, 18);
  gtk_widget_set_margin_start(root, 18);
  gtk_widget_set_margin_end(root, 18);

  GtkWidget *title = gtk_label_new("Archived tasks");
  gtk_widget_add_css_class(title, "card-title");
  gtk_widget_set_halign(title, GTK_ALIGN_START);

  GtkWidget *desc =
      gtk_label_new("Restore tasks to bring them back into your active list.");
  gtk_widget_add_css_class(desc, "task-meta");
  gtk_widget_set_halign(desc, GTK_ALIGN_START);
  gtk_label_set_wrap(GTK_LABEL(desc), TRUE);

  GtkWidget *archived_list = gtk_list_box_new();
  gtk_widget_add_css_class(archived_list, "task-list");
  gtk_list_box_set_selection_mode(GTK_LIST_BOX(archived_list),
                                  GTK_SELECTION_NONE);

  GtkWidget *archived_scroller = gtk_scrolled_window_new();
  gtk_widget_add_css_class(archived_scroller, "task-scroller");
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(archived_scroller),
                                 GTK_POLICY_NEVER,
                                 GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_min_content_height(
      GTK_SCROLLED_WINDOW(archived_scroller),
      260);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(archived_scroller),
                                archived_list);

  GtkWidget *archived_empty_label =
      gtk_label_new("No archived tasks yet.");
  gtk_widget_add_css_class(archived_empty_label, "task-empty");
  gtk_widget_set_halign(archived_empty_label, GTK_ALIGN_START);
  gtk_label_set_wrap(GTK_LABEL(archived_empty_label), TRUE);

  gtk_box_append(GTK_BOX(root), title);
  gtk_box_append(GTK_BOX(root), desc);
  gtk_box_append(GTK_BOX(root), archived_scroller);
  gtk_box_append(GTK_BOX(root), archived_empty_label);

  gtk_window_set_child(GTK_WINDOW(window), root);
  state->archived_window = GTK_WINDOW(window);

  ArchivedDialog *dialog = g_new0(ArchivedDialog, 1);
  dialog->window = GTK_WINDOW(window);
  dialog->list = archived_list;
  dialog->empty_label = archived_empty_label;
  g_object_set_data_full(G_OBJECT(window),
                         "archived-dialog",
                         dialog,
                         archived_dialog_free);
  g_signal_connect(window,
                   "destroy",
                   G_CALLBACK(on_archived_window_destroy),
                   state);

  refresh_task_list(state);
  gtk_window_present(GTK_WINDOW(window));
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

static PomodoroTask *
find_active_task(TaskStore *store)
{
  if (store == NULL) {
    return NULL;
  }

  const GPtrArray *tasks = task_store_get_tasks(store);
  if (tasks == NULL) {
    return NULL;
  }

  for (guint i = 0; i < tasks->len; i++) {
    PomodoroTask *task = g_ptr_array_index((GPtrArray *)tasks, i);
    if (task != NULL && pomodoro_task_get_status(task) == TASK_STATUS_ACTIVE) {
      return task;
    }
  }

  return NULL;
}

static guint
calculate_cycle_minutes(guint cycles)
{
  const guint focus_minutes = 25;
  const guint short_break_minutes = 5;
  const guint long_break_minutes = 15;
  const guint long_break_interval = 4;

  if (cycles < 1) {
    cycles = 1;
  }

  guint total = cycles * focus_minutes;
  guint breaks = cycles;
  guint long_breaks = breaks / long_break_interval;
  guint short_breaks = breaks - long_breaks;

  total += short_breaks * short_break_minutes;
  total += long_breaks * long_break_minutes;

  return total;
}

static char *
format_minutes(guint minutes)
{
  guint hours = minutes / 60;
  guint mins = minutes % 60;

  if (hours == 0) {
    return g_strdup_printf("%um", mins);
  }

  if (mins == 0) {
    return g_strdup_printf("%uh", hours);
  }

  return g_strdup_printf("%uh %um", hours, mins);
}

static char *
format_cycle_summary(guint cycles)
{
  if (cycles < 1) {
    cycles = 1;
  }

  char *duration = format_minutes(calculate_cycle_minutes(cycles));
  char *text = g_strdup_printf("%u cycle%s - %s total",
                               cycles,
                               cycles == 1 ? "" : "s",
                               duration);
  g_free(duration);
  return text;
}

static void
update_repeat_hint(GtkSpinButton *spin, GtkWidget *label)
{
  if (spin == NULL || label == NULL) {
    return;
  }

  guint cycles = (guint)gtk_spin_button_get_value_as_int(spin);
  char *duration = format_minutes(calculate_cycle_minutes(cycles));
  char *text = g_strdup_printf("Estimated total (focus + breaks): %s", duration);
  gtk_label_set_text(GTK_LABEL(label), text);
  g_free(duration);
  g_free(text);
}

static void
on_repeat_spin_changed(GtkSpinButton *spin, gpointer user_data)
{
  update_repeat_hint(spin, GTK_WIDGET(user_data));
}

static char *
format_repeat_label(guint repeat_count)
{
  return format_cycle_summary(repeat_count);
}

static void
update_task_cycle_ui(TaskRowControls *controls)
{
  if (controls == NULL || controls->task == NULL) {
    return;
  }

  guint cycles = pomodoro_task_get_repeat_count(controls->task);

  if (controls->count_label != NULL) {
    char *count = g_strdup_printf("%u", cycles);
    gtk_label_set_text(GTK_LABEL(controls->count_label), count);
    g_free(count);
  }

  if (controls->repeat_label != NULL) {
    char *summary = format_cycle_summary(cycles);
    gtk_label_set_text(GTK_LABEL(controls->repeat_label), summary);
    g_free(summary);
  }
}

static void
set_task_cycles(TaskRowControls *controls, guint cycles)
{
  if (controls == NULL || controls->task == NULL) {
    return;
  }

  pomodoro_task_set_repeat_count(controls->task, cycles);
  update_task_cycle_ui(controls);

  if (controls->state != NULL) {
    save_task_store(controls->state);
    update_current_task_summary(controls->state);
  }
}

static void
on_task_cycle_decrement(GtkButton *button, gpointer user_data)
{
  (void)button;
  TaskRowControls *controls = user_data;
  if (controls == NULL || controls->task == NULL) {
    return;
  }

  guint cycles = pomodoro_task_get_repeat_count(controls->task);
  if (cycles > 1) {
    set_task_cycles(controls, cycles - 1);
  }
}

static void
on_task_cycle_increment(GtkButton *button, gpointer user_data)
{
  (void)button;
  TaskRowControls *controls = user_data;
  if (controls == NULL || controls->task == NULL) {
    return;
  }

  guint cycles = pomodoro_task_get_repeat_count(controls->task);
  if (cycles < 99) {
    set_task_cycles(controls, cycles + 1);
  }
}

static gboolean
focus_title_entry_idle(gpointer data)
{
  GtkWidget *entry = data;
  if (entry != NULL && gtk_widget_get_visible(entry)) {
    gtk_widget_grab_focus(entry);
  }
  return G_SOURCE_REMOVE;
}

static void
set_editing_controls(AppState *state, TaskRowControls *controls)
{
  if (state == NULL) {
    return;
  }

  state->editing_controls = controls;
}

static void
apply_task_title_edit(TaskRowControls *controls)
{
  if (controls == NULL || controls->task == NULL ||
      controls->title_entry == NULL || controls->title_label == NULL) {
    g_warning("apply_task_title_edit: missing controls or widgets");
    return;
  }

  if (!gtk_widget_get_visible(controls->title_entry)) {
    g_debug("apply_task_title_edit: entry not visible; skipping");
    return;
  }

  const char *text = gtk_editable_get_text(GTK_EDITABLE(controls->title_entry));
  if (text == NULL) {
    text = "";
  }

  char *trimmed = g_strstrip(g_strdup(text));
  if (trimmed[0] == '\0') {
    g_free(trimmed);
    trimmed = g_strdup(pomodoro_task_get_title(controls->task));
  }

  const char *current = pomodoro_task_get_title(controls->task);
  if (g_strcmp0(current, trimmed) != 0) {
    g_info("Updating task title to '%s'", trimmed);
    pomodoro_task_set_title(controls->task, trimmed);
  } else {
    g_debug("Task title unchanged");
  }

  gtk_label_set_text(GTK_LABEL(controls->title_label), trimmed);
  gtk_editable_set_text(GTK_EDITABLE(controls->title_entry), trimmed);

  gtk_widget_set_visible(controls->title_entry, FALSE);
  gtk_widget_set_visible(controls->title_label, TRUE);
  controls->title_edit_active = FALSE;
  controls->title_edit_has_focus = FALSE;
  controls->title_edit_started_at = 0;

  g_free(trimmed);

  if (controls->state != NULL) {
    if (controls->state->editing_controls == controls) {
      set_editing_controls(controls->state, NULL);
    }
    save_task_store(controls->state);
    update_current_task_summary(controls->state);
  }
}

static void
start_task_title_edit(TaskRowControls *controls)
{
  if (controls == NULL || controls->task == NULL ||
      controls->title_entry == NULL || controls->title_label == NULL) {
    g_warning("start_task_title_edit: missing controls or widgets");
    return;
  }

  if (controls->state != NULL &&
      controls->state->editing_controls != NULL &&
      controls->state->editing_controls != controls) {
    apply_task_title_edit(controls->state->editing_controls);
  }

  g_info("Entering inline edit for task '%s'",
         pomodoro_task_get_title(controls->task));
  if (controls->state != NULL) {
    set_editing_controls(controls->state, controls);
  }
  controls->title_edit_active = TRUE;
  controls->title_edit_has_focus = FALSE;
  controls->title_edit_started_at = g_get_monotonic_time();
  gtk_editable_set_text(GTK_EDITABLE(controls->title_entry),
                        pomodoro_task_get_title(controls->task));
  gtk_widget_set_visible(controls->title_label, FALSE);
  gtk_widget_set_visible(controls->title_entry, TRUE);
  gtk_widget_grab_focus(controls->title_entry);
  gtk_editable_set_position(GTK_EDITABLE(controls->title_entry), -1);
  g_idle_add(focus_title_entry_idle, controls->title_entry);
}

static void
on_task_title_activate(GtkEntry *entry, gpointer user_data)
{
  (void)entry;
  g_debug("Inline task title activated");
  apply_task_title_edit((TaskRowControls *)user_data);
}

static void
on_task_title_focus_changed(GObject *object, GParamSpec *pspec, gpointer user_data)
{
  (void)pspec;
  GtkWidget *entry = GTK_WIDGET(object);
  TaskRowControls *controls = user_data;
  if (gtk_widget_has_focus(entry)) {
    if (controls != NULL) {
      controls->title_edit_has_focus = TRUE;
    }
    g_debug("Inline task title gained focus");
    return;
  }

  if (controls == NULL || !controls->title_edit_active) {
    return;
  }

  if (!controls->title_edit_has_focus) {
    g_debug("Inline task title lost focus before gaining focus; ignoring");
    return;
  }

  if (controls->title_edit_started_at > 0) {
    gint64 elapsed = g_get_monotonic_time() - controls->title_edit_started_at;
    if (elapsed < 250000) {
      g_debug("Inline task title lost focus too quickly; ignoring");
      return;
    }
  }

  g_debug("Inline task title lost focus");
  apply_task_title_edit(controls);
}

static void
on_window_pressed(GtkGestureClick *gesture,
                  gint n_press,
                  gdouble x,
                  gdouble y,
                  gpointer user_data)
{
  (void)n_press;
  AppState *state = user_data;
  if (state == NULL || state->editing_controls == NULL) {
    return;
  }

  TaskRowControls *controls = state->editing_controls;
  if (controls->title_entry == NULL ||
      !gtk_widget_get_visible(controls->title_entry)) {
    return;
  }

  GtkWidget *root = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
  GtkWidget *target = gtk_widget_pick(root, x, y, GTK_PICK_DEFAULT);
  if (target == NULL) {
    return;
  }

  if (target == controls->title_entry ||
      gtk_widget_is_ancestor(target, controls->title_entry)) {
    return;
  }

  if (controls->edit_button != NULL &&
      (target == controls->edit_button ||
       gtk_widget_is_ancestor(target, controls->edit_button))) {
    return;
  }

  g_debug("Window click outside title entry; applying inline edit");
  apply_task_title_edit(controls);
}

static void
update_current_task_summary(AppState *state)
{
  if (state == NULL || state->current_task_label == NULL) {
    return;
  }

  const PomodoroTask *active_task = find_active_task(state->store);

  if (active_task != NULL) {
    gtk_label_set_text(GTK_LABEL(state->current_task_label),
                       pomodoro_task_get_title(active_task));
    if (state->current_task_meta != NULL) {
      char *repeat_text =
          format_repeat_label(pomodoro_task_get_repeat_count(active_task));
      char *meta_text =
          g_strdup_printf("%s. Ready for the next focus session", repeat_text);
      gtk_label_set_text(GTK_LABEL(state->current_task_meta), meta_text);
      g_free(repeat_text);
      g_free(meta_text);
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

  GtkWidget *text_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
  gtk_widget_set_hexpand(text_box, TRUE);

  GtkWidget *title = gtk_label_new(pomodoro_task_get_title(task));
  gtk_widget_add_css_class(title, "task-item");
  gtk_widget_set_halign(title, GTK_ALIGN_START);
  gtk_widget_set_hexpand(title, TRUE);
  gtk_label_set_wrap(GTK_LABEL(title), TRUE);
  gtk_label_set_xalign(GTK_LABEL(title), 0.0f);

  GtkWidget *title_entry = gtk_entry_new();
  gtk_editable_set_text(GTK_EDITABLE(title_entry), pomodoro_task_get_title(task));
  gtk_widget_add_css_class(title_entry, "task-title-entry");
  gtk_widget_set_hexpand(title_entry, TRUE);
  gtk_widget_set_visible(title_entry, FALSE);

  char *repeat_text = format_repeat_label(pomodoro_task_get_repeat_count(task));
  GtkWidget *repeat_label = gtk_label_new(repeat_text);
  gtk_widget_add_css_class(repeat_label, "task-meta");
  gtk_widget_set_halign(repeat_label, GTK_ALIGN_START);
  gtk_label_set_xalign(GTK_LABEL(repeat_label), 0.0f);
  gtk_label_set_ellipsize(GTK_LABEL(repeat_label), PANGO_ELLIPSIZE_END);
  g_free(repeat_text);

  GtkWidget *cycle_stepper = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
  gtk_widget_add_css_class(cycle_stepper, "cycle-stepper");
  gtk_widget_set_valign(cycle_stepper, GTK_ALIGN_CENTER);

  GtkWidget *decrement_button = gtk_button_new_with_label("-");
  gtk_widget_add_css_class(decrement_button, "stepper-button");

  GtkWidget *count_label = gtk_label_new("");
  gtk_widget_add_css_class(count_label, "task-cycle-count");
  gtk_widget_set_size_request(count_label, 28, -1);
  gtk_widget_set_halign(count_label, GTK_ALIGN_CENTER);

  GtkWidget *increment_button = gtk_button_new_with_label("+");
  gtk_widget_add_css_class(increment_button, "stepper-button");

  gtk_box_append(GTK_BOX(cycle_stepper), decrement_button);
  gtk_box_append(GTK_BOX(cycle_stepper), count_label);
  gtk_box_append(GTK_BOX(cycle_stepper), increment_button);

  if (status == TASK_STATUS_ARCHIVED) {
    gtk_widget_set_sensitive(cycle_stepper, FALSE);
  }

  const char *status_text = "Active";
  if (status == TASK_STATUS_COMPLETED) {
    status_text = "Complete";
  } else if (status == TASK_STATUS_ARCHIVED) {
    status_text = "Archived";
  }

  GtkWidget *status_button = gtk_button_new_with_label(status_text);
  gtk_widget_add_css_class(status_button, "task-status");
  gtk_widget_add_css_class(status_button, "tag");
  gtk_widget_set_valign(status_button, GTK_ALIGN_CENTER);
  if (status == TASK_STATUS_COMPLETED) {
    gtk_widget_add_css_class(status_button, "tag-success");
  } else if (status == TASK_STATUS_ARCHIVED) {
    gtk_widget_add_css_class(status_button, "tag-muted");
    gtk_widget_set_sensitive(status_button, FALSE);
  }
  g_object_set_data(G_OBJECT(status_button), "task", task);
  g_signal_connect(status_button,
                   "clicked",
                   G_CALLBACK(on_task_status_clicked),
                   state);

  GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_widget_set_valign(actions, GTK_ALIGN_CENTER);

  GtkWidget *edit_button = gtk_button_new();
  GtkWidget *edit_icon = gtk_image_new_from_icon_name("document-edit-symbolic");
  gtk_button_set_child(GTK_BUTTON(edit_button), edit_icon);
  gtk_widget_add_css_class(edit_button, "icon-button");
  gtk_widget_set_tooltip_text(edit_button, "Edit task");
  g_object_set_data(G_OBJECT(edit_button), "task", task);
  gtk_box_append(GTK_BOX(actions), edit_button);

  if (status == TASK_STATUS_ARCHIVED) {
    GtkWidget *restore_button = gtk_button_new();
    GtkWidget *restore_icon = gtk_image_new_from_icon_name("edit-undo-symbolic");
    gtk_button_set_child(GTK_BUTTON(restore_button), restore_icon);
    gtk_widget_add_css_class(restore_button, "icon-button");
    gtk_widget_set_tooltip_text(restore_button, "Restore task");
    g_object_set_data(G_OBJECT(restore_button), "task", task);
    g_signal_connect(restore_button,
                     "clicked",
                     G_CALLBACK(on_task_restore_clicked),
                     state);
    gtk_box_append(GTK_BOX(actions), restore_button);
  } else {
    GtkWidget *archive_button = gtk_button_new();
    GtkWidget *archive_icon = gtk_image_new_from_icon_name("archive-symbolic");
    gtk_button_set_child(GTK_BUTTON(archive_button), archive_icon);
    gtk_widget_add_css_class(archive_button, "icon-button");
    gtk_widget_set_tooltip_text(archive_button, "Archive task");
    g_object_set_data(G_OBJECT(archive_button), "task", task);
    g_signal_connect(archive_button,
                     "clicked",
                     G_CALLBACK(on_task_archive_clicked),
                     state);
    gtk_box_append(GTK_BOX(actions), archive_button);
  }

  GtkWidget *delete_button = gtk_button_new();
  GtkWidget *delete_icon = gtk_image_new_from_icon_name("window-close-symbolic");
  gtk_button_set_child(GTK_BUTTON(delete_button), delete_icon);
  gtk_widget_add_css_class(delete_button, "icon-button");
  gtk_widget_add_css_class(delete_button, "icon-danger");
  gtk_widget_set_tooltip_text(delete_button, "Delete task");
  g_object_set_data(G_OBJECT(delete_button), "task", task);
  g_signal_connect(delete_button,
                   "clicked",
                   G_CALLBACK(on_task_delete_clicked),
                   state);
  gtk_box_append(GTK_BOX(actions), delete_button);

  TaskRowControls *controls = g_new0(TaskRowControls, 1);
  controls->state = state;
  controls->task = task;
  controls->repeat_label = repeat_label;
  controls->count_label = count_label;
  controls->title_label = title;
  controls->title_entry = title_entry;
  controls->edit_button = edit_button;
  controls->title_edit_active = FALSE;
  controls->title_edit_has_focus = FALSE;
  controls->title_edit_started_at = 0;

  g_object_set_data_full(G_OBJECT(row),
                         "task-row-controls",
                         controls,
                         task_row_controls_free);
  g_signal_connect(decrement_button,
                   "clicked",
                   G_CALLBACK(on_task_cycle_decrement),
                   controls);
  g_signal_connect(increment_button,
                   "clicked",
                   G_CALLBACK(on_task_cycle_increment),
                   controls);
  g_signal_connect(edit_button,
                   "clicked",
                   G_CALLBACK(on_task_edit_clicked),
                   controls);
  g_signal_connect(title_entry,
                   "activate",
                   G_CALLBACK(on_task_title_activate),
                   controls);
  g_signal_connect(title_entry,
                   "notify::has-focus",
                   G_CALLBACK(on_task_title_focus_changed),
                   controls);
  update_task_cycle_ui(controls);

  gtk_box_append(GTK_BOX(text_box), title);
  gtk_box_append(GTK_BOX(text_box), title_entry);
  gtk_box_append(GTK_BOX(text_box), repeat_label);
  gtk_box_append(GTK_BOX(row), text_box);
  gtk_box_append(GTK_BOX(row), cycle_stepper);
  gtk_box_append(GTK_BOX(row), status_button);
  gtk_box_append(GTK_BOX(row), actions);

  gtk_list_box_append(GTK_LIST_BOX(list), row);
}

static void
refresh_task_list(AppState *state)
{
  if (state == NULL || state->task_list == NULL) {
    return;
  }

  ArchivedDialog *archived_dialog = archived_dialog_get(state);

  GtkWidget *row = gtk_widget_get_first_child(state->task_list);
  while (row != NULL) {
    GtkWidget *next = gtk_widget_get_next_sibling(row);
    gtk_list_box_remove(GTK_LIST_BOX(state->task_list), row);
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

    if (archived_dialog != NULL && archived_dialog->list != NULL) {
      row = gtk_widget_get_first_child(archived_dialog->list);
      while (row != NULL) {
        GtkWidget *next = gtk_widget_get_next_sibling(row);
        gtk_list_box_remove(GTK_LIST_BOX(archived_dialog->list), row);
        row = next;
      }

      for (guint i = 0; i < tasks->len; i++) {
        PomodoroTask *task = g_ptr_array_index((GPtrArray *)tasks, i);
        if (task != NULL &&
            pomodoro_task_get_status(task) == TASK_STATUS_ARCHIVED) {
          append_task_row(state, archived_dialog->list, task);
          archived_count++;
        }
      }
    }
  }

  if (state->task_empty_label != NULL) {
    gtk_widget_set_visible(state->task_empty_label, visible_count == 0);
  }
  if (archived_dialog != NULL && archived_dialog->empty_label != NULL) {
    gtk_widget_set_visible(archived_dialog->empty_label, archived_count == 0);
  }

  update_current_task_summary(state);
}

static void
settings_dialog_update_controls(SettingsDialog *dialog)
{
  if (dialog == NULL || dialog->state == NULL) {
    return;
  }

  dialog->suppress_signals = TRUE;

  TaskArchiveStrategy strategy =
      task_store_get_archive_strategy(dialog->state->store);

  if (dialog->dropdown != NULL) {
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
    gtk_drop_down_set_selected(GTK_DROP_DOWN(dialog->dropdown), selected);
  }

  if (dialog->days_spin != NULL) {
    gtk_spin_button_set_value(dialog->days_spin, (gdouble)strategy.days);
  }
  if (dialog->keep_spin != NULL) {
    gtk_spin_button_set_value(dialog->keep_spin,
                              (gdouble)strategy.keep_latest);
  }

  if (dialog->days_row != NULL) {
    gtk_widget_set_visible(dialog->days_row,
                           strategy.type == TASK_ARCHIVE_AFTER_DAYS);
  }
  if (dialog->keep_row != NULL) {
    gtk_widget_set_visible(dialog->keep_row,
                           strategy.type == TASK_ARCHIVE_KEEP_LATEST);
  }

  dialog->suppress_signals = FALSE;
}

static void
settings_dialog_apply(SettingsDialog *dialog)
{
  if (dialog == NULL || dialog->state == NULL || dialog->state->store == NULL) {
    return;
  }

  TaskArchiveStrategy strategy =
      task_store_get_archive_strategy(dialog->state->store);

  if (dialog->dropdown != NULL) {
    guint selected =
        gtk_drop_down_get_selected(GTK_DROP_DOWN(dialog->dropdown));
    if (selected == 1) {
      strategy.type = TASK_ARCHIVE_IMMEDIATE;
    } else if (selected == 2) {
      strategy.type = TASK_ARCHIVE_KEEP_LATEST;
    } else {
      strategy.type = TASK_ARCHIVE_AFTER_DAYS;
    }
  }

  if (dialog->days_spin != NULL) {
    strategy.days = (guint)gtk_spin_button_get_value_as_int(dialog->days_spin);
  }
  if (dialog->keep_spin != NULL) {
    strategy.keep_latest =
        (guint)gtk_spin_button_get_value_as_int(dialog->keep_spin);
  }

  task_store_set_archive_strategy(dialog->state->store, strategy);
  task_store_apply_archive_policy(dialog->state->store);
  save_task_store(dialog->state);
  refresh_task_list(dialog->state);
  settings_dialog_update_controls(dialog);
}

static void
handle_add_task(AppState *state)
{
  if (state == NULL || state->task_entry == NULL ||
      state->task_repeat_spin == NULL) {
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

  guint repeat_count =
      (guint)gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(state->task_repeat_spin));
  task_store_add(state->store, trimmed, repeat_count);
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
on_task_status_clicked(GtkButton *button, gpointer user_data)
{
  AppState *state = user_data;
  if (state == NULL || button == NULL) {
    return;
  }

  PomodoroTask *task = g_object_get_data(G_OBJECT(button), "task");
  if (task == NULL) {
    return;
  }

  TaskStatus status = pomodoro_task_get_status(task);

  if (status == TASK_STATUS_ACTIVE) {
    show_confirm_dialog(state,
                        "Complete task?",
                        "Are you sure you want to complete this task?",
                        task,
                        NULL,
                        FALSE);
    return;
  }

  if (status == TASK_STATUS_COMPLETED) {
    PomodoroTask *active_task = find_active_task(state->store);
    if (active_task == NULL) {
      task_store_reactivate(state->store, task);
      task_store_apply_archive_policy(state->store);
      save_task_store(state);
      refresh_task_list(state);
      return;
    }

    show_confirm_dialog(
        state,
        "Switch active task?",
        "Are you sure you want to complete the current task and set this task to active state?",
        task,
        active_task,
        TRUE);
  }
}

static void
on_task_edit_clicked(GtkButton *button, gpointer user_data)
{
  (void)button;
  TaskRowControls *controls = user_data;
  if (controls == NULL || controls->task == NULL) {
    g_warning("Edit clicked but task controls are missing");
    return;
  }

  g_debug("Edit icon clicked for task '%s'",
          pomodoro_task_get_title(controls->task));
  if (controls->title_entry != NULL &&
      gtk_widget_get_visible(controls->title_entry)) {
    g_debug("Edit already active; applying inline edit");
    apply_task_title_edit(controls);
  } else {
    g_debug("Starting inline edit");
    start_task_title_edit(controls);
  }
}

static void
on_task_archive_clicked(GtkButton *button, gpointer user_data)
{
  AppState *state = user_data;
  PomodoroTask *task = g_object_get_data(G_OBJECT(button), "task");
  if (state == NULL || task == NULL) {
    return;
  }

  task_store_archive_task(state->store, task);
  task_store_apply_archive_policy(state->store);
  save_task_store(state);
  refresh_task_list(state);
}

static void
on_task_restore_clicked(GtkButton *button, gpointer user_data)
{
  AppState *state = user_data;
  PomodoroTask *task = g_object_get_data(G_OBJECT(button), "task");
  if (state == NULL || task == NULL) {
    return;
  }

  task_store_reactivate(state->store, task);
  task_store_apply_archive_policy(state->store);
  save_task_store(state);
  refresh_task_list(state);
}

static void
on_task_delete_clicked(GtkButton *button, gpointer user_data)
{
  AppState *state = user_data;
  PomodoroTask *task = g_object_get_data(G_OBJECT(button), "task");
  if (state == NULL || task == NULL) {
    return;
  }

  task_store_remove(state->store, task);
  save_task_store(state);
  refresh_task_list(state);
}

static void
on_settings_strategy_changed(GObject *object, GParamSpec *pspec, gpointer user_data)
{
  (void)object;
  (void)pspec;

  SettingsDialog *dialog = user_data;
  if (dialog == NULL || dialog->suppress_signals) {
    return;
  }

  settings_dialog_apply(dialog);
}

static void
on_settings_value_changed(GtkSpinButton *spin, gpointer user_data)
{
  (void)spin;

  SettingsDialog *dialog = user_data;
  if (dialog == NULL || dialog->suppress_signals) {
    return;
  }

  settings_dialog_apply(dialog);
}

static ArchivedDialog *
archived_dialog_get(AppState *state)
{
  if (state == NULL || state->archived_window == NULL) {
    return NULL;
  }

  return g_object_get_data(G_OBJECT(state->archived_window), "archived-dialog");
}

static void
on_show_settings_clicked(GtkButton *button, gpointer user_data)
{
  (void)button;
  show_settings_window((AppState *)user_data);
}

static void
on_show_archived_clicked(GtkButton *button, gpointer user_data)
{
  (void)button;
  show_archived_window((AppState *)user_data);
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

  GtkGesture *window_click = gtk_gesture_click_new();
  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(window_click), 0);
  gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(window_click),
                                             GTK_PHASE_CAPTURE);
  g_signal_connect(window_click,
                   "pressed",
                   G_CALLBACK(on_window_pressed),
                   state);
  gtk_widget_add_controller(window, GTK_EVENT_CONTROLLER(window_click));

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

  GtkWidget *action_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  gtk_widget_set_halign(action_row, GTK_ALIGN_START);

  GtkWidget *settings_button = gtk_button_new_with_label("Archive Settings");
  gtk_widget_add_css_class(settings_button, "btn-secondary");
  gtk_widget_add_css_class(settings_button, "btn-compact");
  g_signal_connect(settings_button,
                   "clicked",
                   G_CALLBACK(on_show_settings_clicked),
                   state);

  GtkWidget *archived_button = gtk_button_new_with_label("Archived Tasks");
  gtk_widget_add_css_class(archived_button, "btn-secondary");
  gtk_widget_add_css_class(archived_button, "btn-compact");
  g_signal_connect(archived_button,
                   "clicked",
                   G_CALLBACK(on_show_archived_clicked),
                   state);

  gtk_box_append(GTK_BOX(action_row), settings_button);
  gtk_box_append(GTK_BOX(action_row), archived_button);
  gtk_box_append(GTK_BOX(root), action_row);

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

  GtkWidget *task_input_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_set_hexpand(task_input_box, TRUE);

  GtkWidget *task_input_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  gtk_widget_set_hexpand(task_input_row, TRUE);
  gtk_widget_add_css_class(task_input_row, "task-input-row");

  GtkWidget *task_entry = gtk_entry_new();
  gtk_widget_set_hexpand(task_entry, TRUE);
  gtk_widget_set_valign(task_entry, GTK_ALIGN_CENTER);
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
  gtk_widget_add_css_class(task_add_button, "task-add");
  gtk_widget_set_valign(task_add_button, GTK_ALIGN_CENTER);
  g_signal_connect(task_add_button,
                   "clicked",
                   G_CALLBACK(on_add_task_clicked),
                   state);

  gtk_box_append(GTK_BOX(task_input_row), task_entry);
  gtk_box_append(GTK_BOX(task_input_row), task_add_button);

  GtkWidget *task_meta_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  gtk_widget_set_hexpand(task_meta_row, TRUE);
  gtk_widget_add_css_class(task_meta_row, "task-input-meta");

  GtkWidget *repeat_group = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_halign(repeat_group, GTK_ALIGN_START);
  gtk_widget_add_css_class(repeat_group, "task-repeat-group");

  GtkWidget *repeat_label = gtk_label_new("Cycles");
  gtk_widget_add_css_class(repeat_label, "task-meta");

  GtkAdjustment *repeat_adjustment = gtk_adjustment_new(1, 1, 99, 1, 5, 0);
  GtkWidget *repeat_spin = gtk_spin_button_new(repeat_adjustment, 1, 0);
  gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(repeat_spin), TRUE);
  gtk_widget_add_css_class(repeat_spin, "task-spin");
  gtk_widget_set_size_request(repeat_spin, 72, -1);
  state->task_repeat_spin = repeat_spin;

  GtkWidget *repeat_hint = gtk_label_new("");
  gtk_widget_add_css_class(repeat_hint, "task-meta");
  gtk_widget_set_hexpand(repeat_hint, TRUE);
  gtk_widget_set_halign(repeat_hint, GTK_ALIGN_END);
  gtk_label_set_xalign(GTK_LABEL(repeat_hint), 1.0f);
  gtk_label_set_ellipsize(GTK_LABEL(repeat_hint), PANGO_ELLIPSIZE_END);
  gtk_widget_set_tooltip_text(
      repeat_hint,
      "Assumes each cycle is 25m focus + 5m break; every 4th break is 15m.");
  state->task_repeat_hint = repeat_hint;

  g_signal_connect(repeat_spin,
                   "value-changed",
                   G_CALLBACK(on_repeat_spin_changed),
                   repeat_hint);
  update_repeat_hint(GTK_SPIN_BUTTON(repeat_spin), repeat_hint);

  gtk_box_append(GTK_BOX(repeat_group), repeat_label);
  gtk_box_append(GTK_BOX(repeat_group), repeat_spin);

  gtk_box_append(GTK_BOX(task_meta_row), repeat_group);
  gtk_box_append(GTK_BOX(task_meta_row), repeat_hint);

  gtk_box_append(GTK_BOX(task_input_box), task_input_row);
  gtk_box_append(GTK_BOX(task_input_box), task_meta_row);

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

  gtk_box_append(GTK_BOX(tasks_card), tasks_title);
  gtk_box_append(GTK_BOX(tasks_card), task_input_box);
  gtk_box_append(GTK_BOX(tasks_card), task_scroller);
  gtk_box_append(GTK_BOX(tasks_card), task_empty_label);

  gtk_box_append(GTK_BOX(task_section), tasks_card);
  gtk_box_append(GTK_BOX(root), task_section);

  gtk_window_set_child(GTK_WINDOW(window), root);
  gtk_window_present(GTK_WINDOW(window));

  refresh_task_list(state);

  g_info("Main window presented");
}

int
main(int argc, char *argv[])
{
  install_logging();
  install_crash_handler();
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
