#include "ui/dialogs.h"

#include "ui/task_list.h"

typedef struct {
  AppState *state;
  GtkWindow *window;
  GtkWidget *dropdown;
  GtkWidget *days_row;
  GtkWidget *keep_row;
  GtkSpinButton *days_spin;
  GtkSpinButton *keep_spin;
  gboolean suppress_signals;
} ArchiveSettingsDialog;

static void archive_settings_update_controls(ArchiveSettingsDialog *dialog);

static void
archive_settings_dialog_free(gpointer data)
{
  ArchiveSettingsDialog *dialog = data;
  if (dialog == NULL) {
    return;
  }

  g_free(dialog);
}

static void
on_archive_settings_window_destroy(GtkWidget *widget, gpointer user_data)
{
  (void)widget;
  ArchiveSettingsDialog *dialog = user_data;
  if (dialog == NULL) {
    return;
  }

  dialog->suppress_signals = TRUE;
  g_info("Archive settings window destroyed");
  if (dialog->state != NULL) {
    dialog->state->archive_settings_window = NULL;
  }
}

static gboolean
on_archive_settings_window_close(GtkWindow *window, gpointer user_data)
{
  (void)window;
  ArchiveSettingsDialog *dialog = user_data;
  if (dialog != NULL) {
    dialog->suppress_signals = TRUE;
    g_info("Archive settings window close requested");
  }
  return FALSE;
}

static void
on_archive_settings_strategy_changed(GObject *object,
                                     GParamSpec *pspec,
                                     gpointer user_data)
{
  (void)object;
  (void)pspec;

  ArchiveSettingsDialog *dialog = user_data;
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
  archive_settings_update_controls(dialog);
}

static void
on_archive_settings_value_changed(GtkSpinButton *spin, gpointer user_data)
{
  (void)spin;

  ArchiveSettingsDialog *dialog = user_data;
  if (dialog == NULL || dialog->suppress_signals || dialog->state == NULL ||
      dialog->state->store == NULL) {
    return;
  }

  on_archive_settings_strategy_changed(NULL, NULL, dialog);
}

static void
archive_settings_update_controls(ArchiveSettingsDialog *dialog)
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
show_archive_settings_window(AppState *state)
{
  if (state == NULL) {
    return;
  }

  if (state->archive_settings_window != NULL) {
    gtk_window_present(state->archive_settings_window);
    return;
  }

  GtkApplication *app = gtk_window_get_application(state->window);
  GtkWidget *window = gtk_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(window), "Archive Settings");
  if (state->archived_window != NULL) {
    gtk_window_set_transient_for(GTK_WINDOW(window), state->archived_window);
  } else {
    gtk_window_set_transient_for(GTK_WINDOW(window), state->window);
  }
  gtk_window_set_modal(GTK_WINDOW(window), TRUE);
  gtk_window_set_default_size(GTK_WINDOW(window), 420, 260);

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
  state->archive_settings_window = GTK_WINDOW(window);

  ArchiveSettingsDialog *dialog = g_new0(ArchiveSettingsDialog, 1);
  dialog->state = state;
  dialog->window = GTK_WINDOW(window);
  dialog->dropdown = archive_dropdown;
  dialog->days_row = archive_days_row;
  dialog->keep_row = archive_keep_row;
  dialog->days_spin = GTK_SPIN_BUTTON(archive_days_spin);
  dialog->keep_spin = GTK_SPIN_BUTTON(archive_keep_spin);

  g_signal_connect(archive_dropdown,
                   "notify::selected",
                   G_CALLBACK(on_archive_settings_strategy_changed),
                   dialog);
  g_signal_connect(archive_days_spin,
                   "value-changed",
                   G_CALLBACK(on_archive_settings_value_changed),
                   dialog);
  g_signal_connect(archive_keep_spin,
                   "value-changed",
                   G_CALLBACK(on_archive_settings_value_changed),
                   dialog);

  g_object_set_data_full(G_OBJECT(window),
                         "archive-settings-dialog",
                         dialog,
                         archive_settings_dialog_free);

  g_signal_connect(window,
                   "close-request",
                   G_CALLBACK(on_archive_settings_window_close),
                   dialog);
  g_signal_connect(window,
                   "destroy",
                   G_CALLBACK(on_archive_settings_window_destroy),
                   dialog);

  archive_settings_update_controls(dialog);
  gtk_window_present(GTK_WINDOW(window));
}

void
dialogs_on_show_archive_settings_clicked(GtkButton *button, gpointer user_data)
{
  (void)button;
  show_archive_settings_window((AppState *)user_data);
}

void
dialogs_cleanup_archive_settings(AppState *state)
{
  if (state == NULL || state->archive_settings_window == NULL) {
    return;
  }

  ArchiveSettingsDialog *dialog = g_object_get_data(
      G_OBJECT(state->archive_settings_window),
      "archive-settings-dialog");
  if (dialog != NULL) {
    dialog->state = NULL;
  }

  gtk_window_destroy(state->archive_settings_window);
  state->archive_settings_window = NULL;
}
