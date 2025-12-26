#pragma once

#include <gtk/gtk.h>

#include "app/app_state.h"
#include "core/task_store.h"

void dialogs_on_show_settings_clicked(GtkButton *button, gpointer user_data);
void dialogs_on_show_archived_clicked(GtkButton *button, gpointer user_data);
void dialogs_show_confirm(AppState *state,
                          const char *title_text,
                          const char *body_text,
                          PomodoroTask *task,
                          PomodoroTask *active_task,
                          gboolean switch_active);
void dialogs_cleanup_settings(AppState *state);
void dialogs_cleanup_archived(AppState *state);

gboolean dialogs_get_archived_targets(AppState *state,
                                      GtkWidget **list,
                                      GtkWidget **empty_label);
