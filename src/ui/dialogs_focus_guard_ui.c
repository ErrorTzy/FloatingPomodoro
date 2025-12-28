#include "ui/dialogs_focus_guard_internal.h"

#include "config.h"

void
focus_guard_settings_append(TimerSettingsDialog *dialog,
                            GtkWidget *focus_root,
                            GtkWidget *chrome_root)
{
  if (dialog == NULL || focus_root == NULL) {
    return;
  }

  GtkWidget *guard_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_add_css_class(guard_card, "card");

  GtkWidget *guard_title = gtk_label_new("Focus guard");
  gtk_widget_add_css_class(guard_title, "card-title");
  gtk_widget_set_halign(guard_title, GTK_ALIGN_START);

  GtkWidget *guard_desc = gtk_label_new(
      "Warn when blacklisted apps take focus during a running session.");
  gtk_widget_add_css_class(guard_desc, "task-meta");
  gtk_widget_set_halign(guard_desc, GTK_ALIGN_START);
  gtk_label_set_wrap(GTK_LABEL(guard_desc), TRUE);

  GtkWidget *guard_grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(guard_grid), 10);
  gtk_grid_set_column_spacing(GTK_GRID(guard_grid), 16);

  GtkWidget *guard_global_label = gtk_label_new("Global app usage stats");
  gtk_widget_add_css_class(guard_global_label, "setting-label");
  gtk_widget_set_halign(guard_global_label, GTK_ALIGN_START);
  gtk_widget_set_hexpand(guard_global_label, TRUE);
  GtkWidget *guard_global_check = gtk_check_button_new();
  gtk_widget_set_halign(guard_global_check, GTK_ALIGN_END);
  gtk_widget_set_tooltip_text(guard_global_check,
                              "Track app usage continuously while the app runs.");

  GtkWidget *guard_warning_label = gtk_label_new("Warnings");
  gtk_widget_add_css_class(guard_warning_label, "setting-label");
  gtk_widget_set_halign(guard_warning_label, GTK_ALIGN_START);
  gtk_widget_set_hexpand(guard_warning_label, TRUE);
  GtkWidget *guard_warning_check = gtk_check_button_new();
  gtk_widget_set_halign(guard_warning_check, GTK_ALIGN_END);

  GtkWidget *guard_interval_label = gtk_label_new("Check interval (sec)");
  gtk_widget_add_css_class(guard_interval_label, "setting-label");
  gtk_widget_set_halign(guard_interval_label, GTK_ALIGN_START);
  gtk_widget_set_hexpand(guard_interval_label, TRUE);
  GtkWidget *guard_interval_spin = gtk_spin_button_new_with_range(1, 60, 1);
  gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(guard_interval_spin), TRUE);
  gtk_widget_add_css_class(guard_interval_spin, "setting-spin");
  gtk_widget_set_halign(guard_interval_spin, GTK_ALIGN_END);

  gtk_grid_attach(GTK_GRID(guard_grid), guard_global_label, 0, 0, 1, 1);
  gtk_grid_attach(GTK_GRID(guard_grid), guard_global_check, 1, 0, 1, 1);
  gtk_grid_attach(GTK_GRID(guard_grid), guard_warning_label, 0, 1, 1, 1);
  gtk_grid_attach(GTK_GRID(guard_grid), guard_warning_check, 1, 1, 1, 1);
  gtk_grid_attach(GTK_GRID(guard_grid), guard_interval_label, 0, 2, 1, 1);
  gtk_grid_attach(GTK_GRID(guard_grid), guard_interval_spin, 1, 2, 1, 1);

  gtk_box_append(GTK_BOX(guard_card), guard_title);
  gtk_box_append(GTK_BOX(guard_card), guard_desc);
  gtk_box_append(GTK_BOX(guard_card), guard_grid);

  GtkWidget *blacklist_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_add_css_class(blacklist_card, "card");

  GtkWidget *blacklist_title = gtk_label_new("Blacklisted apps");
  gtk_widget_add_css_class(blacklist_title, "card-title");
  gtk_widget_set_halign(blacklist_title, GTK_ALIGN_START);

  GtkWidget *blacklist_desc = gtk_label_new(
      "Add distractions here to get warned during focus sessions.");
  gtk_widget_add_css_class(blacklist_desc, "task-meta");
  gtk_widget_set_halign(blacklist_desc, GTK_ALIGN_START);
  gtk_label_set_wrap(GTK_LABEL(blacklist_desc), TRUE);

  GtkWidget *guard_entry_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_hexpand(guard_entry_row, TRUE);
  GtkWidget *guard_entry = gtk_entry_new();
  gtk_widget_set_hexpand(guard_entry, TRUE);
  gtk_entry_set_placeholder_text(GTK_ENTRY(guard_entry),
                                 "Add app name (e.g. Discord, Chrome)");
  gtk_widget_add_css_class(guard_entry, "task-entry");

  GtkWidget *guard_add_button = gtk_button_new();
  gtk_widget_add_css_class(guard_add_button, "icon-button");
  gtk_widget_set_size_request(guard_add_button, 32, 32);
  GtkWidget *guard_add_icon =
      gtk_image_new_from_icon_name("list-add-symbolic");
  gtk_image_set_pixel_size(GTK_IMAGE(guard_add_icon), 18);
  gtk_button_set_child(GTK_BUTTON(guard_add_button), guard_add_icon);
  gtk_widget_set_tooltip_text(guard_add_button, "Add to blacklist");

  gtk_box_append(GTK_BOX(guard_entry_row), guard_entry);
  gtk_box_append(GTK_BOX(guard_entry_row), guard_add_button);

  GtkWidget *guard_active_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_hexpand(guard_active_row, TRUE);

  GtkWidget *guard_active_label = gtk_label_new("Active app: unavailable");
  gtk_widget_add_css_class(guard_active_label, "task-meta");
  gtk_widget_set_halign(guard_active_label, GTK_ALIGN_START);
  gtk_label_set_ellipsize(GTK_LABEL(guard_active_label), PANGO_ELLIPSIZE_END);
  gtk_widget_set_hexpand(guard_active_label, TRUE);

  GtkWidget *guard_use_button = gtk_button_new_with_label("Use active app");
  gtk_widget_add_css_class(guard_use_button, "btn-secondary");
  gtk_widget_add_css_class(guard_use_button, "btn-compact");
  gtk_widget_set_halign(guard_use_button, GTK_ALIGN_END);
  gtk_widget_set_tooltip_text(guard_use_button,
                              "Add the currently focused app to the blacklist");

  gtk_box_append(GTK_BOX(guard_active_row), guard_active_label);
  gtk_box_append(GTK_BOX(guard_active_row), guard_use_button);

  GtkWidget *guard_list = gtk_list_box_new();
  gtk_widget_add_css_class(guard_list, "focus-guard-list");
  gtk_list_box_set_selection_mode(GTK_LIST_BOX(guard_list),
                                  GTK_SELECTION_NONE);

  GtkWidget *guard_scroller = gtk_scrolled_window_new();
  gtk_widget_add_css_class(guard_scroller, "task-scroller");
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(guard_scroller),
                                 GTK_POLICY_NEVER,
                                 GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_min_content_height(
      GTK_SCROLLED_WINDOW(guard_scroller),
      140);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(guard_scroller), guard_list);

  GtkWidget *guard_empty_label = gtk_label_new("No blacklisted apps yet.");
  gtk_widget_add_css_class(guard_empty_label, "focus-guard-empty");
  gtk_widget_set_halign(guard_empty_label, GTK_ALIGN_START);
  gtk_label_set_wrap(GTK_LABEL(guard_empty_label), TRUE);

  gtk_box_append(GTK_BOX(blacklist_card), blacklist_title);
  gtk_box_append(GTK_BOX(blacklist_card), blacklist_desc);
  gtk_box_append(GTK_BOX(blacklist_card), guard_entry_row);
  gtk_box_append(GTK_BOX(blacklist_card), guard_active_row);
  gtk_box_append(GTK_BOX(blacklist_card), guard_scroller);
  gtk_box_append(GTK_BOX(blacklist_card), guard_empty_label);

  gtk_box_append(GTK_BOX(focus_root), guard_card);
  gtk_box_append(GTK_BOX(focus_root), blacklist_card);

  gboolean ollama_available = FALSE;
#if HAVE_CHROME_OLLAMA
  ollama_available = dialog->state != NULL &&
                     dialog->state->focus_guard != NULL &&
                     focus_guard_is_ollama_available(dialog->state->focus_guard);
#endif

  GtkCheckButton *chrome_check = NULL;
  GtkSpinButton *chrome_port_spin = NULL;
  GtkDropDown *ollama_dropdown = NULL;
  GtkButton *ollama_refresh_button = NULL;
  GtkWidget *ollama_status_label = NULL;
  GtkWidget *trafilatura_status_label = NULL;
  GtkEntry *trafilatura_python_entry = NULL;

  dialog->focus_guard_ollama_section = NULL;

  if (ollama_available && chrome_root != NULL) {
    GtkWidget *chrome_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_add_css_class(chrome_card, "card");

    GtkWidget *chrome_title = gtk_label_new("Chrome relevance");
    gtk_widget_add_css_class(chrome_title, "card-title");
    gtk_widget_set_halign(chrome_title, GTK_ALIGN_START);

    GtkWidget *chrome_desc = gtk_label_new(
        "When Chrome is active during a focus session, check if the page matches the current task.");
    gtk_widget_add_css_class(chrome_desc, "task-meta");
    gtk_widget_set_halign(chrome_desc, GTK_ALIGN_START);
    gtk_label_set_wrap(GTK_LABEL(chrome_desc), TRUE);

    GtkWidget *chrome_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(chrome_grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(chrome_grid), 16);

    GtkWidget *chrome_enable_label = gtk_label_new("Enable relevance check");
    gtk_widget_add_css_class(chrome_enable_label, "setting-label");
    gtk_widget_set_halign(chrome_enable_label, GTK_ALIGN_START);
    gtk_widget_set_hexpand(chrome_enable_label, TRUE);
    GtkWidget *chrome_enable_check = gtk_check_button_new();
    gtk_widget_set_halign(chrome_enable_check, GTK_ALIGN_END);

    GtkWidget *model_label = gtk_label_new("Ollama model");
    gtk_widget_add_css_class(model_label, "setting-label");
    gtk_widget_set_halign(model_label, GTK_ALIGN_START);
    gtk_widget_set_hexpand(model_label, TRUE);

    GtkWidget *model_dropdown = focus_guard_create_model_dropdown(dialog);
    gtk_widget_add_css_class(model_dropdown, "setting-dropdown");
    gtk_widget_set_hexpand(model_dropdown, TRUE);

    GtkWidget *model_refresh = gtk_button_new();
    gtk_widget_add_css_class(model_refresh, "icon-button");
    GtkWidget *refresh_icon = gtk_image_new_from_icon_name("view-refresh-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(refresh_icon), 18);
    gtk_button_set_child(GTK_BUTTON(model_refresh), refresh_icon);
    gtk_widget_set_tooltip_text(model_refresh, "Refresh models");

    GtkWidget *model_controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_hexpand(model_controls, TRUE);
    gtk_box_append(GTK_BOX(model_controls), model_dropdown);
    gtk_box_append(GTK_BOX(model_controls), model_refresh);

    GtkWidget *port_label = gtk_label_new("Chrome debug port");
    gtk_widget_add_css_class(port_label, "setting-label");
    gtk_widget_set_halign(port_label, GTK_ALIGN_START);
    gtk_widget_set_hexpand(port_label, TRUE);
    GtkWidget *port_spin = gtk_spin_button_new_with_range(1, 65535, 1);
    gtk_widget_add_css_class(port_spin, "setting-spin");
    gtk_widget_set_halign(port_spin, GTK_ALIGN_END);
    gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(port_spin), TRUE);

    GtkWidget *python_label = gtk_label_new("Trafilatura python");
    gtk_widget_add_css_class(python_label, "setting-label");
    gtk_widget_set_halign(python_label, GTK_ALIGN_START);
    gtk_widget_set_hexpand(python_label, TRUE);
    GtkWidget *python_entry = gtk_entry_new();
    gtk_widget_add_css_class(python_entry, "task-entry");
    gtk_widget_set_hexpand(python_entry, TRUE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(python_entry),
                                   "python3 or /path/to/python");
    gtk_widget_set_tooltip_text(
        python_entry,
        "Leave empty to use python3 on PATH. Set a venv/conda Python if needed.");

    gtk_grid_attach(GTK_GRID(chrome_grid), chrome_enable_label, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(chrome_grid), chrome_enable_check, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(chrome_grid), model_label, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(chrome_grid), model_controls, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(chrome_grid), port_label, 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(chrome_grid), port_spin, 1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(chrome_grid), python_label, 0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(chrome_grid), python_entry, 1, 3, 1, 1);

    GtkWidget *chrome_hint = gtk_label_new(
        "Chrome must be started with --remote-debugging-port to enable page checks.");
    gtk_widget_add_css_class(chrome_hint, "task-meta");
    gtk_widget_set_halign(chrome_hint, GTK_ALIGN_START);
    gtk_label_set_wrap(GTK_LABEL(chrome_hint), TRUE);

    GtkWidget *trafilatura_label = gtk_label_new("Trafilatura status: checking...");
    gtk_widget_add_css_class(trafilatura_label, "task-meta");
    gtk_widget_set_halign(trafilatura_label, GTK_ALIGN_START);
    gtk_label_set_wrap(GTK_LABEL(trafilatura_label), TRUE);

    GtkWidget *status_label = gtk_label_new("");
    gtk_widget_add_css_class(status_label, "task-meta");
    gtk_widget_set_halign(status_label, GTK_ALIGN_START);
    gtk_label_set_wrap(GTK_LABEL(status_label), TRUE);
    gtk_widget_set_visible(status_label, FALSE);

    gtk_box_append(GTK_BOX(chrome_card), chrome_title);
    gtk_box_append(GTK_BOX(chrome_card), chrome_desc);
    gtk_box_append(GTK_BOX(chrome_card), chrome_grid);
    gtk_box_append(GTK_BOX(chrome_card), chrome_hint);
    gtk_box_append(GTK_BOX(chrome_card), trafilatura_label);
    gtk_box_append(GTK_BOX(chrome_card), status_label);
    gtk_box_append(GTK_BOX(chrome_root), chrome_card);

    chrome_check = GTK_CHECK_BUTTON(chrome_enable_check);
    chrome_port_spin = GTK_SPIN_BUTTON(port_spin);
    ollama_dropdown = GTK_DROP_DOWN(model_dropdown);
    ollama_refresh_button = GTK_BUTTON(model_refresh);
    ollama_status_label = status_label;
    trafilatura_status_label = trafilatura_label;
    trafilatura_python_entry = GTK_ENTRY(python_entry);
    dialog->focus_guard_ollama_section = chrome_root;
  }

  dialog->focus_guard_global_check = GTK_CHECK_BUTTON(guard_global_check);
  dialog->focus_guard_warnings_check = GTK_CHECK_BUTTON(guard_warning_check);
  dialog->focus_guard_interval_spin = GTK_SPIN_BUTTON(guard_interval_spin);
  dialog->focus_guard_list = guard_list;
  dialog->focus_guard_empty_label = guard_empty_label;
  dialog->focus_guard_entry = guard_entry;
  dialog->focus_guard_active_label = guard_active_label;
  dialog->focus_guard_chrome_check = chrome_check;
  dialog->focus_guard_chrome_port_spin = chrome_port_spin;
  dialog->focus_guard_ollama_dropdown = ollama_dropdown;
  dialog->focus_guard_ollama_refresh_button = ollama_refresh_button;
  dialog->focus_guard_ollama_status_label = ollama_status_label;
  dialog->focus_guard_trafilatura_status_label = trafilatura_status_label;
  dialog->focus_guard_trafilatura_python_entry = trafilatura_python_entry;

  g_signal_connect(guard_interval_spin,
                   "value-changed",
                   G_CALLBACK(on_focus_guard_interval_changed),
                   dialog);
  g_signal_connect(guard_global_check,
                   "toggled",
                   G_CALLBACK(on_focus_guard_global_toggled),
                   dialog);
  g_signal_connect(guard_warning_check,
                   "toggled",
                   G_CALLBACK(on_focus_guard_warnings_toggled),
                   dialog);
  g_signal_connect(guard_add_button,
                   "clicked",
                   G_CALLBACK(on_focus_guard_add_clicked),
                   dialog);
  g_signal_connect(guard_entry,
                   "activate",
                   G_CALLBACK(on_focus_guard_entry_activate),
                   dialog);
  g_signal_connect(guard_use_button,
                   "clicked",
                   G_CALLBACK(on_focus_guard_use_active_clicked),
                   dialog);

  if (ollama_available && dialog->focus_guard_ollama_section != NULL) {
    g_signal_connect(ollama_dropdown,
                     "notify::selected",
                     G_CALLBACK(on_focus_guard_model_changed),
                     dialog);
    g_signal_connect(ollama_refresh_button,
                     "clicked",
                     G_CALLBACK(on_focus_guard_ollama_refresh_clicked),
                     dialog);
    g_signal_connect(chrome_check,
                     "toggled",
                     G_CALLBACK(on_focus_guard_chrome_toggled),
                     dialog);
    g_signal_connect(chrome_port_spin,
                     "value-changed",
                     G_CALLBACK(on_focus_guard_chrome_port_changed),
                     dialog);
    g_signal_connect(trafilatura_python_entry,
                     "changed",
                     G_CALLBACK(on_focus_guard_trafilatura_python_changed),
                     dialog);
  }

  if (ollama_available && dialog->focus_guard_ollama_section != NULL) {
    focus_guard_refresh_models(dialog);
  }
}
