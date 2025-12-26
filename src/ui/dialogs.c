#include "ui/dialogs.h"

#include "ui/task_list.h"

typedef struct {
  AppState *state;
  PomodoroTask *task;
  PomodoroTask *active_task;
  GtkWidget *window;
  gboolean switch_active;
} ConfirmDialog;

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

static void settings_dialog_update_controls(SettingsDialog *dialog);

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
  task_list_save_store(dialog->state);
  task_list_refresh(dialog->state);

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

void
dialogs_show_confirm(AppState *state,
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
on_settings_strategy_changed(GObject *object,
                             GParamSpec *pspec,
                             gpointer user_data)
{
  (void)object;
  (void)pspec;

  SettingsDialog *dialog = user_data;
  if (dialog == NULL || dialog->suppress_signals || dialog->state == NULL ||
      dialog->state->store == NULL) {
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
  task_list_save_store(dialog->state);
  task_list_refresh(dialog->state);
  settings_dialog_update_controls(dialog);
}

static void
on_settings_value_changed(GtkSpinButton *spin, gpointer user_data)
{
  (void)spin;

  SettingsDialog *dialog = user_data;
  if (dialog == NULL || dialog->suppress_signals || dialog->state == NULL ||
      dialog->state->store == NULL) {
    return;
  }

  on_settings_strategy_changed(NULL, NULL, dialog);
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

static ArchivedDialog *
archived_dialog_get(AppState *state)
{
  if (state == NULL || state->archived_window == NULL) {
    return NULL;
  }

  return g_object_get_data(G_OBJECT(state->archived_window), "archived-dialog");
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

  task_list_refresh(state);
  gtk_window_present(GTK_WINDOW(window));
}

void
dialogs_on_show_settings_clicked(GtkButton *button, gpointer user_data)
{
  (void)button;
  show_settings_window((AppState *)user_data);
}

void
dialogs_on_show_archived_clicked(GtkButton *button, gpointer user_data)
{
  (void)button;
  show_archived_window((AppState *)user_data);
}

void
dialogs_cleanup_settings(AppState *state)
{
  if (state == NULL || state->settings_window == NULL) {
    return;
  }

  SettingsDialog *dialog =
      g_object_get_data(G_OBJECT(state->settings_window), "settings-dialog");
  if (dialog != NULL) {
    dialog->state = NULL;
  }

  gtk_window_destroy(state->settings_window);
  state->settings_window = NULL;
}

void
dialogs_cleanup_archived(AppState *state)
{
  if (state == NULL || state->archived_window == NULL) {
    return;
  }

  ArchivedDialog *dialog = archived_dialog_get(state);
  if (dialog != NULL) {
    dialog->window = NULL;
    dialog->list = NULL;
    dialog->empty_label = NULL;
  }

  gtk_window_destroy(state->archived_window);
  state->archived_window = NULL;
}

gboolean
dialogs_get_archived_targets(AppState *state,
                             GtkWidget **list,
                             GtkWidget **empty_label)
{
  if (list != NULL) {
    *list = NULL;
  }
  if (empty_label != NULL) {
    *empty_label = NULL;
  }

  ArchivedDialog *dialog = archived_dialog_get(state);
  if (dialog == NULL || dialog->list == NULL || dialog->empty_label == NULL) {
    return FALSE;
  }

  if (list != NULL) {
    *list = dialog->list;
  }
  if (empty_label != NULL) {
    *empty_label = dialog->empty_label;
  }

  return TRUE;
}
