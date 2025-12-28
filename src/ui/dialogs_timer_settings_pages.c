#include "ui/dialogs_timer_settings_internal.h"

GtkWidget *
timer_settings_build_timer_page(TimerSettingsDialog *dialog)
{
  if (dialog == NULL) {
    return NULL;
  }

  GtkWidget *timer_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
  gtk_widget_add_css_class(timer_page, "settings-page");
  gtk_widget_set_margin_top(timer_page, 4);
  gtk_widget_set_margin_bottom(timer_page, 8);
  gtk_widget_set_margin_start(timer_page, 2);
  gtk_widget_set_margin_end(timer_page, 2);

  GtkWidget *timer_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_add_css_class(timer_card, "card");

  GtkWidget *timer_title = gtk_label_new("Timer cycle");
  gtk_widget_add_css_class(timer_title, "card-title");
  gtk_widget_set_halign(timer_title, GTK_ALIGN_START);

  GtkWidget *timer_desc =
      gtk_label_new("Adjust the cadence of focus and recovery.");
  gtk_widget_add_css_class(timer_desc, "task-meta");
  gtk_widget_set_halign(timer_desc, GTK_ALIGN_START);
  gtk_label_set_wrap(GTK_LABEL(timer_desc), TRUE);

  GtkWidget *timer_grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(timer_grid), 10);
  gtk_grid_set_column_spacing(GTK_GRID(timer_grid), 16);

  GtkWidget *focus_label = gtk_label_new("Focus minutes");
  gtk_widget_add_css_class(focus_label, "setting-label");
  gtk_widget_set_halign(focus_label, GTK_ALIGN_START);
  gtk_widget_set_hexpand(focus_label, TRUE);
  GtkWidget *focus_spin = gtk_spin_button_new_with_range(1, 120, 1);
  gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(focus_spin), TRUE);
  gtk_widget_add_css_class(focus_spin, "setting-spin");
  gtk_widget_set_halign(focus_spin, GTK_ALIGN_END);

  GtkWidget *short_label = gtk_label_new("Short break");
  gtk_widget_add_css_class(short_label, "setting-label");
  gtk_widget_set_halign(short_label, GTK_ALIGN_START);
  gtk_widget_set_hexpand(short_label, TRUE);
  GtkWidget *short_spin = gtk_spin_button_new_with_range(1, 30, 1);
  gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(short_spin), TRUE);
  gtk_widget_add_css_class(short_spin, "setting-spin");
  gtk_widget_set_halign(short_spin, GTK_ALIGN_END);

  GtkWidget *long_label = gtk_label_new("Long break");
  gtk_widget_add_css_class(long_label, "setting-label");
  gtk_widget_set_halign(long_label, GTK_ALIGN_START);
  gtk_widget_set_hexpand(long_label, TRUE);
  GtkWidget *long_spin = gtk_spin_button_new_with_range(1, 60, 1);
  gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(long_spin), TRUE);
  gtk_widget_add_css_class(long_spin, "setting-spin");
  gtk_widget_set_halign(long_spin, GTK_ALIGN_END);

  GtkWidget *interval_label = gtk_label_new("Long break every (sessions)");
  gtk_widget_add_css_class(interval_label, "setting-label");
  gtk_widget_set_halign(interval_label, GTK_ALIGN_START);
  gtk_widget_set_hexpand(interval_label, TRUE);
  GtkWidget *interval_spin = gtk_spin_button_new_with_range(1, 12, 1);
  gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(interval_spin), TRUE);
  gtk_widget_add_css_class(interval_spin, "setting-spin");
  gtk_widget_set_halign(interval_spin, GTK_ALIGN_END);

  gtk_grid_attach(GTK_GRID(timer_grid), focus_label, 0, 0, 1, 1);
  gtk_grid_attach(GTK_GRID(timer_grid), focus_spin, 1, 0, 1, 1);
  gtk_grid_attach(GTK_GRID(timer_grid), short_label, 0, 1, 1, 1);
  gtk_grid_attach(GTK_GRID(timer_grid), short_spin, 1, 1, 1, 1);
  gtk_grid_attach(GTK_GRID(timer_grid), long_label, 0, 2, 1, 1);
  gtk_grid_attach(GTK_GRID(timer_grid), long_spin, 1, 2, 1, 1);
  gtk_grid_attach(GTK_GRID(timer_grid), interval_label, 0, 3, 1, 1);
  gtk_grid_attach(GTK_GRID(timer_grid), interval_spin, 1, 3, 1, 1);

  gtk_box_append(GTK_BOX(timer_card), timer_title);
  gtk_box_append(GTK_BOX(timer_card), timer_desc);
  gtk_box_append(GTK_BOX(timer_card), timer_grid);

  gtk_box_append(GTK_BOX(timer_page), timer_card);

  GtkWidget *timer_scroller = gtk_scrolled_window_new();
  gtk_widget_add_css_class(timer_scroller, "settings-scroller");
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(timer_scroller),
                                 GTK_POLICY_NEVER,
                                 GTK_POLICY_AUTOMATIC);
  gtk_widget_set_vexpand(timer_scroller, TRUE);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(timer_scroller), timer_page);

  dialog->focus_spin = GTK_SPIN_BUTTON(focus_spin);
  dialog->short_spin = GTK_SPIN_BUTTON(short_spin);
  dialog->long_spin = GTK_SPIN_BUTTON(long_spin);
  dialog->interval_spin = GTK_SPIN_BUTTON(interval_spin);

  g_signal_connect(focus_spin,
                   "value-changed",
                   G_CALLBACK(on_timer_settings_changed),
                   dialog);
  g_signal_connect(short_spin,
                   "value-changed",
                   G_CALLBACK(on_timer_settings_changed),
                   dialog);
  g_signal_connect(long_spin,
                   "value-changed",
                   G_CALLBACK(on_timer_settings_changed),
                   dialog);
  g_signal_connect(interval_spin,
                   "value-changed",
                   G_CALLBACK(on_timer_settings_changed),
                   dialog);

  return timer_scroller;
}

GtkWidget *
timer_settings_build_app_page(TimerSettingsDialog *dialog)
{
  if (dialog == NULL) {
    return NULL;
  }

  GtkWidget *app_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
  gtk_widget_add_css_class(app_page, "settings-page");
  gtk_widget_set_margin_top(app_page, 4);
  gtk_widget_set_margin_bottom(app_page, 8);
  gtk_widget_set_margin_start(app_page, 2);
  gtk_widget_set_margin_end(app_page, 2);

  GtkWidget *app_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_add_css_class(app_card, "card");

  GtkWidget *app_title = gtk_label_new("Startup & tray");
  gtk_widget_add_css_class(app_title, "card-title");
  gtk_widget_set_halign(app_title, GTK_ALIGN_START);

  GtkWidget *app_desc =
      gtk_label_new("Configure how the app launches and hides.");
  gtk_widget_add_css_class(app_desc, "task-meta");
  gtk_widget_set_halign(app_desc, GTK_ALIGN_START);
  gtk_label_set_wrap(GTK_LABEL(app_desc), TRUE);

  GtkWidget *app_grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(app_grid), 10);
  gtk_grid_set_column_spacing(GTK_GRID(app_grid), 16);

  GtkWidget *autostart_label = gtk_label_new("Autostart on login");
  gtk_widget_add_css_class(autostart_label, "setting-label");
  gtk_widget_set_halign(autostart_label, GTK_ALIGN_START);
  gtk_widget_set_hexpand(autostart_label, TRUE);
  GtkWidget *autostart_check = gtk_check_button_new();
  gtk_widget_set_halign(autostart_check, GTK_ALIGN_END);

  GtkWidget *autostart_tray_label =
      gtk_label_new("Start in tray when autostarting");
  gtk_widget_add_css_class(autostart_tray_label, "setting-label");
  gtk_widget_set_halign(autostart_tray_label, GTK_ALIGN_START);
  gtk_widget_set_hexpand(autostart_tray_label, TRUE);
  gtk_label_set_wrap(GTK_LABEL(autostart_tray_label), TRUE);
  GtkWidget *autostart_tray_check = gtk_check_button_new();
  gtk_widget_set_halign(autostart_tray_check, GTK_ALIGN_END);

  GtkWidget *minimize_label = gtk_label_new("Minimize to tray");
  gtk_widget_add_css_class(minimize_label, "setting-label");
  gtk_widget_set_halign(minimize_label, GTK_ALIGN_START);
  gtk_widget_set_hexpand(minimize_label, TRUE);
  GtkWidget *minimize_check = gtk_check_button_new();
  gtk_widget_set_halign(minimize_check, GTK_ALIGN_END);

  GtkWidget *tray_label = gtk_label_new("Close to tray");
  gtk_widget_add_css_class(tray_label, "setting-label");
  gtk_widget_set_halign(tray_label, GTK_ALIGN_START);
  gtk_widget_set_hexpand(tray_label, TRUE);
  GtkWidget *tray_check = gtk_check_button_new();
  gtk_widget_set_halign(tray_check, GTK_ALIGN_END);

  gtk_grid_attach(GTK_GRID(app_grid), autostart_label, 0, 0, 1, 1);
  gtk_grid_attach(GTK_GRID(app_grid), autostart_check, 1, 0, 1, 1);
  gtk_grid_attach(GTK_GRID(app_grid), autostart_tray_label, 0, 1, 1, 1);
  gtk_grid_attach(GTK_GRID(app_grid), autostart_tray_check, 1, 1, 1, 1);
  gtk_grid_attach(GTK_GRID(app_grid), minimize_label, 0, 2, 1, 1);
  gtk_grid_attach(GTK_GRID(app_grid), minimize_check, 1, 2, 1, 1);
  gtk_grid_attach(GTK_GRID(app_grid), tray_label, 0, 3, 1, 1);
  gtk_grid_attach(GTK_GRID(app_grid), tray_check, 1, 3, 1, 1);

  gtk_box_append(GTK_BOX(app_card), app_title);
  gtk_box_append(GTK_BOX(app_card), app_desc);
  gtk_box_append(GTK_BOX(app_card), app_grid);

  gtk_box_append(GTK_BOX(app_page), app_card);

  GtkWidget *data_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_add_css_class(data_card, "card");

  GtkWidget *data_title = gtk_label_new("Data & maintenance");
  gtk_widget_add_css_class(data_title, "card-title");
  gtk_widget_set_halign(data_title, GTK_ALIGN_START);

  GtkWidget *data_desc = gtk_label_new(
      "Run bulk actions on settings, tasks, or usage stats.");
  gtk_widget_add_css_class(data_desc, "task-meta");
  gtk_widget_set_halign(data_desc, GTK_ALIGN_START);
  gtk_label_set_wrap(GTK_LABEL(data_desc), TRUE);

  GtkWidget *data_grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(data_grid), 10);
  gtk_grid_set_column_spacing(GTK_GRID(data_grid), 16);

  GtkWidget *reset_label = gtk_label_new("Reset settings to defaults");
  gtk_widget_add_css_class(reset_label, "setting-label");
  gtk_widget_set_halign(reset_label, GTK_ALIGN_START);
  gtk_widget_set_hexpand(reset_label, TRUE);
  GtkWidget *reset_button = gtk_button_new_with_label("Reset");
  gtk_widget_add_css_class(reset_button, "btn-secondary");
  gtk_widget_add_css_class(reset_button, "btn-compact");
  gtk_widget_set_halign(reset_button, GTK_ALIGN_END);

  GtkWidget *archive_all_label = gtk_label_new("Archive all tasks");
  gtk_widget_add_css_class(archive_all_label, "setting-label");
  gtk_widget_set_halign(archive_all_label, GTK_ALIGN_START);
  gtk_widget_set_hexpand(archive_all_label, TRUE);
  GtkWidget *archive_all_button = gtk_button_new_with_label("Archive all");
  gtk_widget_add_css_class(archive_all_button, "btn-secondary");
  gtk_widget_add_css_class(archive_all_button, "btn-compact");
  gtk_widget_set_halign(archive_all_button, GTK_ALIGN_END);

  GtkWidget *delete_archived_label = gtk_label_new("Delete archived tasks");
  gtk_widget_add_css_class(delete_archived_label, "setting-label");
  gtk_widget_set_halign(delete_archived_label, GTK_ALIGN_START);
  gtk_widget_set_hexpand(delete_archived_label, TRUE);
  GtkWidget *delete_archived_button =
      gtk_button_new_with_label("Delete archived");
  gtk_widget_add_css_class(delete_archived_button, "btn-danger");
  gtk_widget_add_css_class(delete_archived_button, "btn-compact");
  gtk_widget_set_halign(delete_archived_button, GTK_ALIGN_END);

  GtkWidget *delete_stats_label = gtk_label_new("Delete all usage stats");
  gtk_widget_add_css_class(delete_stats_label, "setting-label");
  gtk_widget_set_halign(delete_stats_label, GTK_ALIGN_START);
  gtk_widget_set_hexpand(delete_stats_label, TRUE);
  GtkWidget *delete_stats_button = gtk_button_new_with_label("Delete stats");
  gtk_widget_add_css_class(delete_stats_button, "btn-danger");
  gtk_widget_add_css_class(delete_stats_button, "btn-compact");
  gtk_widget_set_halign(delete_stats_button, GTK_ALIGN_END);

  gtk_grid_attach(GTK_GRID(data_grid), reset_label, 0, 0, 1, 1);
  gtk_grid_attach(GTK_GRID(data_grid), reset_button, 1, 0, 1, 1);
  gtk_grid_attach(GTK_GRID(data_grid), archive_all_label, 0, 1, 1, 1);
  gtk_grid_attach(GTK_GRID(data_grid), archive_all_button, 1, 1, 1, 1);
  gtk_grid_attach(GTK_GRID(data_grid), delete_archived_label, 0, 2, 1, 1);
  gtk_grid_attach(GTK_GRID(data_grid), delete_archived_button, 1, 2, 1, 1);
  gtk_grid_attach(GTK_GRID(data_grid), delete_stats_label, 0, 3, 1, 1);
  gtk_grid_attach(GTK_GRID(data_grid), delete_stats_button, 1, 3, 1, 1);

  gtk_box_append(GTK_BOX(data_card), data_title);
  gtk_box_append(GTK_BOX(data_card), data_desc);
  gtk_box_append(GTK_BOX(data_card), data_grid);

  gtk_box_append(GTK_BOX(app_page), data_card);

  GtkWidget *app_scroller = gtk_scrolled_window_new();
  gtk_widget_add_css_class(app_scroller, "settings-scroller");
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(app_scroller),
                                 GTK_POLICY_NEVER,
                                 GTK_POLICY_AUTOMATIC);
  gtk_widget_set_vexpand(app_scroller, TRUE);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(app_scroller), app_page);

  dialog->close_to_tray_check = GTK_CHECK_BUTTON(tray_check);
  dialog->autostart_check = GTK_CHECK_BUTTON(autostart_check);
  dialog->autostart_start_in_tray_check =
      GTK_CHECK_BUTTON(autostart_tray_check);
  dialog->minimize_to_tray_check = GTK_CHECK_BUTTON(minimize_check);

  g_signal_connect(tray_check,
                   "toggled",
                   G_CALLBACK(on_app_settings_toggled),
                   dialog);
  g_signal_connect(autostart_check,
                   "toggled",
                   G_CALLBACK(on_app_settings_toggled),
                   dialog);
  g_signal_connect(autostart_tray_check,
                   "toggled",
                   G_CALLBACK(on_app_settings_toggled),
                   dialog);
  g_signal_connect(minimize_check,
                   "toggled",
                   G_CALLBACK(on_app_settings_toggled),
                   dialog);
  g_signal_connect(reset_button,
                   "clicked",
                   G_CALLBACK(on_app_reset_settings_clicked),
                   dialog);
  g_signal_connect(archive_all_button,
                   "clicked",
                   G_CALLBACK(on_app_archive_all_clicked),
                   dialog);
  g_signal_connect(delete_archived_button,
                   "clicked",
                   G_CALLBACK(on_app_delete_archived_clicked),
                   dialog);
  g_signal_connect(delete_stats_button,
                   "clicked",
                   G_CALLBACK(on_app_delete_stats_clicked),
                   dialog);

  return app_scroller;
}
