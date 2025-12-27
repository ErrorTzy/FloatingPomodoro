#pragma once

#include <gio/gio.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define FOCUS_GUARD_TYPE_SETTINGS_MODEL (focus_guard_settings_model_get_type())

G_DECLARE_FINAL_TYPE(FocusGuardSettingsModel,
                     focus_guard_settings_model,
                     FOCUS_GUARD,
                     SETTINGS_MODEL,
                     GObject)

FocusGuardSettingsModel *focus_guard_settings_model_new(void);

GtkStringList *focus_guard_settings_model_get_ollama_models(
    FocusGuardSettingsModel *model);
void focus_guard_settings_model_replace_ollama_models(
    FocusGuardSettingsModel *model,
    GPtrArray *models);

void focus_guard_settings_model_set_refresh_cancellable(
    FocusGuardSettingsModel *model,
    GCancellable *cancellable);
GCancellable *focus_guard_settings_model_get_refresh_cancellable(
    FocusGuardSettingsModel *model);
void focus_guard_settings_model_cancel_refresh(FocusGuardSettingsModel *model);

void focus_guard_settings_model_set_last_external(FocusGuardSettingsModel *model,
                                                  const char *value);
const char *focus_guard_settings_model_get_last_external(
    FocusGuardSettingsModel *model);

G_END_DECLS
