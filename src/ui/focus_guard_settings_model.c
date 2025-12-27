#include "ui/focus_guard_settings_model.h"

struct _FocusGuardSettingsModel {
  GObject parent_instance;
  GtkStringList *ollama_models;
  GCancellable *refresh_cancellable;
  char *last_external;
};

G_DEFINE_TYPE(FocusGuardSettingsModel, focus_guard_settings_model, G_TYPE_OBJECT)

static void
focus_guard_settings_model_dispose(GObject *object)
{
  FocusGuardSettingsModel *self = FOCUS_GUARD_SETTINGS_MODEL(object);

  if (self->refresh_cancellable != NULL) {
    g_cancellable_cancel(self->refresh_cancellable);
    g_clear_object(&self->refresh_cancellable);
  }

  G_OBJECT_CLASS(focus_guard_settings_model_parent_class)->dispose(object);
}

static void
focus_guard_settings_model_finalize(GObject *object)
{
  FocusGuardSettingsModel *self = FOCUS_GUARD_SETTINGS_MODEL(object);

  g_clear_object(&self->ollama_models);
  g_clear_pointer(&self->last_external, g_free);

  G_OBJECT_CLASS(focus_guard_settings_model_parent_class)->finalize(object);
}

static void
focus_guard_settings_model_class_init(FocusGuardSettingsModelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS(klass);
  object_class->dispose = focus_guard_settings_model_dispose;
  object_class->finalize = focus_guard_settings_model_finalize;
}

static void
focus_guard_settings_model_init(FocusGuardSettingsModel *self)
{
  self->ollama_models = gtk_string_list_new(NULL);
}

FocusGuardSettingsModel *
focus_guard_settings_model_new(void)
{
  return g_object_new(FOCUS_GUARD_TYPE_SETTINGS_MODEL, NULL);
}

GtkStringList *
focus_guard_settings_model_get_ollama_models(FocusGuardSettingsModel *model)
{
  if (model == NULL) {
    return NULL;
  }

  return model->ollama_models;
}

void
focus_guard_settings_model_replace_ollama_models(FocusGuardSettingsModel *model,
                                                 GPtrArray *models)
{
  if (model == NULL || model->ollama_models == NULL) {
    return;
  }

  guint existing = g_list_model_get_n_items(G_LIST_MODEL(model->ollama_models));
  if (existing > 0) {
    gtk_string_list_splice(model->ollama_models, 0, existing, NULL);
  }

  if (models != NULL) {
    for (guint i = 0; i < models->len; i++) {
      const char *item = g_ptr_array_index(models, i);
      if (item != NULL && *item != '\0') {
        gtk_string_list_append(model->ollama_models, item);
      }
    }
  }
}

void
focus_guard_settings_model_set_refresh_cancellable(
    FocusGuardSettingsModel *model,
    GCancellable *cancellable)
{
  if (model == NULL) {
    return;
  }

  g_set_object(&model->refresh_cancellable, cancellable);
}

GCancellable *
focus_guard_settings_model_get_refresh_cancellable(FocusGuardSettingsModel *model)
{
  if (model == NULL) {
    return NULL;
  }

  return model->refresh_cancellable;
}

void
focus_guard_settings_model_cancel_refresh(FocusGuardSettingsModel *model)
{
  if (model == NULL || model->refresh_cancellable == NULL) {
    return;
  }

  g_cancellable_cancel(model->refresh_cancellable);
  g_clear_object(&model->refresh_cancellable);
}

void
focus_guard_settings_model_set_last_external(FocusGuardSettingsModel *model,
                                             const char *value)
{
  if (model == NULL) {
    return;
  }

  g_free(model->last_external);
  model->last_external = value != NULL ? g_strdup(value) : NULL;
}

const char *
focus_guard_settings_model_get_last_external(FocusGuardSettingsModel *model)
{
  if (model == NULL) {
    return NULL;
  }

  return model->last_external;
}
