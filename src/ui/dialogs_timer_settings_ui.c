#include "ui/dialogs_timer_settings_internal.h"

#include "focus/focus_guard.h"

GtkWidget *timer_settings_build_timer_page(TimerSettingsDialog *dialog);
GtkWidget *timer_settings_build_app_page(TimerSettingsDialog *dialog);

void
timer_settings_show_window(AppState *state)
{
  if (state == NULL) {
    return;
  }

  if (state->timer_settings_window != NULL) {
    gtk_window_present(state->timer_settings_window);
    return;
  }

  GtkApplication *app = gtk_window_get_application(state->window);
  GtkWidget *window = gtk_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(window), "Settings");
  gtk_window_set_transient_for(GTK_WINDOW(window), state->window);
  gtk_window_set_modal(GTK_WINDOW(window), FALSE);
  gtk_window_set_default_size(GTK_WINDOW(window), 720, 620);

  GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
  gtk_widget_set_margin_top(root, 20);
  gtk_widget_set_margin_bottom(root, 20);
  gtk_widget_set_margin_start(root, 20);
  gtk_widget_set_margin_end(root, 20);
  gtk_widget_add_css_class(root, "settings-root");

  GtkWidget *header = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
  GtkWidget *title = gtk_label_new("Settings");
  gtk_widget_add_css_class(title, "settings-title");
  gtk_widget_set_halign(title, GTK_ALIGN_START);

  GtkWidget *desc = gtk_label_new(
      "Customize your timer and focus guard. Changes apply instantly.");
  gtk_widget_add_css_class(desc, "settings-subtitle");
  gtk_widget_set_halign(desc, GTK_ALIGN_START);
  gtk_label_set_wrap(GTK_LABEL(desc), TRUE);

  gtk_box_append(GTK_BOX(header), title);
  gtk_box_append(GTK_BOX(header), desc);
  gtk_box_append(GTK_BOX(root), header);

  GtkWidget *stack = gtk_stack_new();
  gtk_stack_set_transition_type(GTK_STACK(stack),
                                GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
  gtk_stack_set_transition_duration(GTK_STACK(stack), 180);
  gtk_widget_set_vexpand(stack, TRUE);

  GtkWidget *switcher = gtk_stack_switcher_new();
  gtk_stack_switcher_set_stack(GTK_STACK_SWITCHER(switcher), GTK_STACK(stack));
  gtk_widget_add_css_class(switcher, "settings-switcher");
  gtk_widget_set_halign(switcher, GTK_ALIGN_START);
  gtk_box_append(GTK_BOX(root), switcher);
  gtk_box_append(GTK_BOX(root), stack);

  TimerSettingsDialog *dialog = g_new0(TimerSettingsDialog, 1);
  dialog->state = state;
  dialog->window = GTK_WINDOW(window);
  dialog->focus_guard_model = focus_guard_settings_model_new();

  GtkWidget *timer_scroller = timer_settings_build_timer_page(dialog);
  GtkWidget *app_scroller = timer_settings_build_app_page(dialog);

  GtkWidget *focus_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
  gtk_widget_add_css_class(focus_page, "settings-page");
  gtk_widget_set_margin_top(focus_page, 4);
  gtk_widget_set_margin_bottom(focus_page, 8);
  gtk_widget_set_margin_start(focus_page, 2);
  gtk_widget_set_margin_end(focus_page, 2);

  GtkWidget *focus_scroller = gtk_scrolled_window_new();
  gtk_widget_add_css_class(focus_scroller, "settings-scroller");
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(focus_scroller),
                                 GTK_POLICY_NEVER,
                                 GTK_POLICY_AUTOMATIC);
  gtk_widget_set_vexpand(focus_scroller, TRUE);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(focus_scroller), focus_page);

  gboolean ollama_available = state->focus_guard != NULL &&
                              focus_guard_is_ollama_available(state->focus_guard);
  GtkWidget *chrome_page = NULL;
  GtkWidget *chrome_scroller = NULL;
  if (ollama_available) {
    chrome_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_add_css_class(chrome_page, "settings-page");
    gtk_widget_set_margin_top(chrome_page, 4);
    gtk_widget_set_margin_bottom(chrome_page, 8);
    gtk_widget_set_margin_start(chrome_page, 2);
    gtk_widget_set_margin_end(chrome_page, 2);

    chrome_scroller = gtk_scrolled_window_new();
    gtk_widget_add_css_class(chrome_scroller, "settings-scroller");
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(chrome_scroller),
                                   GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(chrome_scroller, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(chrome_scroller),
                                  chrome_page);
  }

  focus_guard_settings_append(dialog, focus_page, chrome_page);

  gtk_stack_add_titled(GTK_STACK(stack), timer_scroller, "timer", "Timer");
  gtk_stack_add_titled(GTK_STACK(stack), app_scroller, "app", "App");
  gtk_stack_add_titled(GTK_STACK(stack), focus_scroller, "focus", "Focus guard");
  if (chrome_scroller != NULL && dialog->focus_guard_ollama_section != NULL) {
    gtk_stack_add_titled(GTK_STACK(stack), chrome_scroller, "chrome", "Chrome");
  }

  gtk_window_set_child(GTK_WINDOW(window), root);
  state->timer_settings_window = GTK_WINDOW(window);

  g_object_set_data_full(G_OBJECT(window),
                         "timer-settings-dialog",
                         dialog,
                         timer_settings_dialog_free);

  g_signal_connect(window,
                   "close-request",
                   G_CALLBACK(on_timer_settings_window_close),
                   dialog);
  g_signal_connect(window,
                   "destroy",
                   G_CALLBACK(on_timer_settings_window_destroy),
                   dialog);

  timer_settings_update_controls(dialog);
  focus_guard_start_active_monitor(dialog);
  gtk_window_present(GTK_WINDOW(window));
}
