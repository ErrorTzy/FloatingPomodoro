#pragma once

#include "focus/focus_guard.h"
#include "ui/dialogs_timer_settings_internal.h"

void focus_guard_apply_settings(TimerSettingsDialog *dialog);

void focus_guard_update_empty_label(TimerSettingsDialog *dialog);
gboolean focus_guard_blacklist_contains(TimerSettingsDialog *dialog,
                                        const char *value);
gboolean focus_guard_is_self_app(const char *app_name);
void focus_guard_append_blacklist_row(TimerSettingsDialog *dialog,
                                      const char *value);
void focus_guard_clear_list(GtkWidget *list);
char **focus_guard_collect_blacklist(TimerSettingsDialog *dialog);

void focus_guard_set_ollama_status(TimerSettingsDialog *dialog, const char *text);
GtkStringList *focus_guard_get_model_list(TimerSettingsDialog *dialog);
GtkWidget *focus_guard_create_model_dropdown(TimerSettingsDialog *dialog);
char *focus_guard_get_selected_model(TimerSettingsDialog *dialog);
void focus_guard_update_ollama_toggle(TimerSettingsDialog *dialog);
void focus_guard_apply_model_selection(TimerSettingsDialog *dialog,
                                       const FocusGuardConfig *config);
void focus_guard_apply_models_to_dropdown(TimerSettingsDialog *dialog,
                                          GPtrArray *models);
void focus_guard_refresh_models(TimerSettingsDialog *dialog);

void on_focus_guard_interval_changed(GtkSpinButton *spin, gpointer user_data);
void on_focus_guard_global_toggled(GtkCheckButton *button, gpointer user_data);
void on_focus_guard_warnings_toggled(GtkCheckButton *button, gpointer user_data);
void on_focus_guard_chrome_toggled(GtkCheckButton *button, gpointer user_data);
void on_focus_guard_chrome_port_changed(GtkSpinButton *spin,
                                        gpointer user_data);
void on_focus_guard_trafilatura_python_changed(GtkEditable *editable,
                                                gpointer user_data);
void on_focus_guard_model_changed(GObject *object,
                                  GParamSpec *pspec,
                                  gpointer user_data);
void on_focus_guard_ollama_refresh_clicked(GtkButton *button, gpointer user_data);
void on_focus_guard_add_clicked(GtkButton *button, gpointer user_data);
void on_focus_guard_entry_activate(GtkEntry *entry, gpointer user_data);
void on_focus_guard_remove_clicked(GtkButton *button, gpointer user_data);
void on_focus_guard_use_active_clicked(GtkButton *button, gpointer user_data);
