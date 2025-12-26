#pragma once

#include <gio/gio.h>
#include <gtk/gtk.h>

#include "app/app_state.h"
#include "core/pomodoro_timer.h"

#define TRAY_SNI_OBJECT_PATH "/StatusNotifierItem"
#define TRAY_MENU_OBJECT_PATH "/StatusNotifierItem/Menu"
#define TRAY_SNI_WATCHER_NAME "org.kde.StatusNotifierWatcher"
#define TRAY_SNI_WATCHER_PATH "/StatusNotifierWatcher"
#define TRAY_SNI_WATCHER_IFACE "org.kde.StatusNotifierWatcher"
#define TRAY_SNI_ITEM_IFACE "org.kde.StatusNotifierItem"
#define TRAY_DBUSMENU_IFACE "com.canonical.dbusmenu"

typedef struct _TrayItem TrayItem;

struct _TrayItem {
  AppState *state;
  GtkApplication *app;
  GDBusConnection *connection;
  guint sni_registration_id;
  guint menu_registration_id;
  guint watcher_id;
  guint menu_revision;
  gboolean has_state;
  PomodoroTimerState last_timer_state;
  PomodoroPhase last_phase;
  gboolean last_has_task;
  gboolean last_overlay_visible;
  GVariant *icon_pixmap;
};

gboolean tray_item_has_task(AppState *state);
void tray_action_present(AppState *state, GtkApplication *app);

GVariant *tray_icon_pixmap_empty(void);
GVariant *tray_icon_pixmap_draw(int size);

void tray_menu_register(TrayItem *tray);
void tray_menu_unregister(TrayItem *tray);
void tray_menu_emit_layout_updated(TrayItem *tray);
void tray_menu_emit_props_updated(TrayItem *tray);

void tray_sni_register(TrayItem *tray);
void tray_sni_unregister(TrayItem *tray);
void tray_sni_watch(TrayItem *tray);
void tray_sni_unwatch(TrayItem *tray);
