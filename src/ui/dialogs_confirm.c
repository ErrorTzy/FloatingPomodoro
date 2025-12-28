#include "ui/dialogs.h"

#include "ui/task_list.h"

typedef struct {
  AppState *state;
  PomodoroTask *task;
  GtkWidget *window;
  DialogConfirmAction action;
} ConfirmDialog;

typedef struct {
  AppState *state;
  GtkWidget *window;
  DialogConfirmCallback callback;
  gpointer user_data;
  GDestroyNotify user_data_free;
} ConfirmActionDialog;

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
confirm_action_dialog_free(gpointer data)
{
  ConfirmActionDialog *dialog = data;
  if (dialog == NULL) {
    return;
  }

  if (dialog->user_data_free != NULL) {
    dialog->user_data_free(dialog->user_data);
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

  if (dialog->action == DIALOG_CONFIRM_ACTIVATE_TASK) {
    task_store_set_active(dialog->state->store, dialog->task);
  } else {
    task_store_complete(dialog->state->store, dialog->task);
  }

  task_store_apply_archive_policy(dialog->state->store);
  task_list_save_store(dialog->state);
  task_list_refresh(dialog->state);

  gtk_window_destroy(GTK_WINDOW(dialog->window));
}

static void
apply_confirm_action_dialog(ConfirmActionDialog *dialog)
{
  if (dialog == NULL) {
    return;
  }

  if (dialog->callback != NULL && dialog->state != NULL) {
    dialog->callback(dialog->state, dialog->user_data);
  }

  gtk_window_destroy(GTK_WINDOW(dialog->window));
}

static void
on_confirm_ok_clicked(GtkButton *button, gpointer user_data)
{
  (void)button;
  apply_confirm_dialog((ConfirmDialog *)user_data);
}

static void
on_confirm_action_ok_clicked(GtkButton *button, gpointer user_data)
{
  (void)button;
  apply_confirm_action_dialog((ConfirmActionDialog *)user_data);
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
                     DialogConfirmAction action)
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
  dialog_state->window = dialog;
  dialog_state->action = action;

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

void
dialogs_show_confirm_action(AppState *state,
                            const char *title_text,
                            const char *body_text,
                            DialogConfirmCallback callback,
                            gpointer user_data,
                            GDestroyNotify user_data_free)
{
  if (state == NULL) {
    if (user_data_free != NULL) {
      user_data_free(user_data);
    }
    return;
  }

  GtkWidget *dialog = gtk_window_new();
  gtk_window_set_title(GTK_WINDOW(dialog), title_text);
  gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
  gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);
  GtkWindow *parent =
      state->timer_settings_window != NULL ? state->timer_settings_window
                                           : state->window;
  if (parent != NULL) {
    gtk_window_set_transient_for(GTK_WINDOW(dialog), parent);
  }
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

  ConfirmActionDialog *dialog_state = g_new0(ConfirmActionDialog, 1);
  dialog_state->state = state;
  dialog_state->window = dialog;
  dialog_state->callback = callback;
  dialog_state->user_data = user_data;
  dialog_state->user_data_free = user_data_free;

  g_object_set_data_full(G_OBJECT(dialog),
                         "confirm-action-dialog",
                         dialog_state,
                         confirm_action_dialog_free);

  g_signal_connect(cancel,
                   "clicked",
                   G_CALLBACK(on_confirm_cancel_clicked),
                   dialog);
  g_signal_connect(confirm,
                   "clicked",
                   G_CALLBACK(on_confirm_action_ok_clicked),
                   dialog_state);

  gtk_window_present(GTK_WINDOW(dialog));
}
