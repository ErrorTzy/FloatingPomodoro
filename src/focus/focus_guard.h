#pragma once

#include <gtk/gtk.h>

#include "app/app_state.h"
#include "focus/focus_guard_config.h"

typedef struct _FocusGuard FocusGuard;

FocusGuard *focus_guard_create(AppState *state, FocusGuardConfig config);
void focus_guard_destroy(FocusGuard *guard);
void focus_guard_apply_config(FocusGuard *guard, FocusGuardConfig config);
FocusGuardConfig focus_guard_get_config(const FocusGuard *guard);
