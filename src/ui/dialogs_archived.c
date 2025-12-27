#include "ui/dialogs.h"

#include "ui/task_list.h"

typedef struct {
  GtkWindow *window;
  GtkWidget *list;
  GtkWidget *empty_label;
} ArchivedDialog;

static GtkWidget *
create_dialog_icon_button(const char *icon_name,
                          int size,
                          const char *tooltip)
{
  GtkWidget *button = gtk_button_new();
  gtk_widget_add_css_class(button, "icon-button");
  gtk_widget_set_size_request(button, 34, 34);
  gtk_widget_set_valign(button, GTK_ALIGN_CENTER);

  GtkWidget *icon = gtk_image_new_from_icon_name(icon_name);
  gtk_image_set_pixel_size(GTK_IMAGE(icon), size);
  gtk_button_set_child(GTK_BUTTON(button), icon);

  if (tooltip != NULL) {
    gtk_widget_set_tooltip_text(button, tooltip);
    gtk_accessible_update_property(GTK_ACCESSIBLE(button),
                                   GTK_ACCESSIBLE_PROPERTY_LABEL,
                                   tooltip,
                                   -1);
  }

  return button;
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

  GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_hexpand(header, TRUE);
  gtk_widget_set_halign(header, GTK_ALIGN_FILL);

  GtkWidget *title = gtk_label_new("Archived tasks");
  gtk_widget_add_css_class(title, "card-title");
  gtk_widget_set_halign(title, GTK_ALIGN_START);
  gtk_widget_set_hexpand(title, TRUE);

  GtkWidget *settings_button =
      create_dialog_icon_button("pomodoro-edit-symbolic", 18, "Archive settings");
  g_signal_connect(settings_button,
                   "clicked",
                   G_CALLBACK(dialogs_on_show_archive_settings_clicked),
                   state);

  gtk_box_append(GTK_BOX(header), title);
  gtk_box_append(GTK_BOX(header), settings_button);

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

  gtk_box_append(GTK_BOX(root), header);
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
dialogs_on_show_archived_clicked(GtkButton *button, gpointer user_data)
{
  (void)button;
  show_archived_window((AppState *)user_data);
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
