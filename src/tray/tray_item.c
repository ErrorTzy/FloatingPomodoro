#include "tray/tray_item.h"

#include <cairo.h>
#include <gio/gio.h>

#include "config.h"
#include "core/pomodoro_timer.h"
#include "core/task_store.h"
#include "overlay/overlay_window.h"
#include "ui/main_window.h"

#define SNI_OBJECT_PATH "/StatusNotifierItem"
#define MENU_OBJECT_PATH "/StatusNotifierItem/Menu"
#define SNI_WATCHER_NAME "org.kde.StatusNotifierWatcher"
#define SNI_WATCHER_PATH "/StatusNotifierWatcher"
#define SNI_WATCHER_IFACE "org.kde.StatusNotifierWatcher"
#define SNI_ITEM_IFACE "org.kde.StatusNotifierItem"
#define DBUSMENU_IFACE "com.canonical.dbusmenu"

enum {
  MENU_ID_ROOT = 0,
  MENU_ID_TOGGLE = 1,
  MENU_ID_SKIP = 2,
  MENU_ID_STOP = 3,
  MENU_ID_SEPARATOR = 4,
  MENU_ID_OVERLAY_TOGGLE = 5,
  MENU_ID_OPEN_APP = 6,
  MENU_ID_QUIT = 7
};

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

static const char *tray_icon_name = "alarm-symbolic";

static const char sni_introspection_xml[] =
    "<node>"
    " <interface name='org.kde.StatusNotifierItem'>"
    "  <method name='Activate'>"
    "   <arg name='x' type='i' direction='in'/>"
    "   <arg name='y' type='i' direction='in'/>"
    "  </method>"
    "  <method name='SecondaryActivate'>"
    "   <arg name='x' type='i' direction='in'/>"
    "   <arg name='y' type='i' direction='in'/>"
    "  </method>"
    "  <method name='ContextMenu'>"
    "   <arg name='x' type='i' direction='in'/>"
    "   <arg name='y' type='i' direction='in'/>"
    "  </method>"
    "  <method name='Scroll'>"
    "   <arg name='delta' type='i' direction='in'/>"
    "   <arg name='orientation' type='s' direction='in'/>"
    "  </method>"
    "  <property name='Category' type='s' access='read'/>"
    "  <property name='Id' type='s' access='read'/>"
    "  <property name='Title' type='s' access='read'/>"
    "  <property name='Status' type='s' access='read'/>"
    "  <property name='WindowId' type='u' access='read'/>"
    "  <property name='IconName' type='s' access='read'/>"
    "  <property name='IconPixmap' type='a(iiay)' access='read'/>"
    "  <property name='IconThemePath' type='s' access='read'/>"
    "  <property name='OverlayIconName' type='s' access='read'/>"
    "  <property name='OverlayIconPixmap' type='a(iiay)' access='read'/>"
    "  <property name='AttentionIconName' type='s' access='read'/>"
    "  <property name='AttentionIconPixmap' type='a(iiay)' access='read'/>"
    "  <property name='AttentionMovieName' type='s' access='read'/>"
    "  <property name='ToolTip' type='(sa(iiay)ss)' access='read'/>"
    "  <property name='ItemIsMenu' type='b' access='read'/>"
    "  <property name='Menu' type='o' access='read'/>"
    " </interface>"
    "</node>";

static const char menu_introspection_xml[] =
    "<node>"
    " <interface name='com.canonical.dbusmenu'>"
    "  <method name='GetLayout'>"
    "   <arg name='parentId' type='i' direction='in'/>"
    "   <arg name='recursionDepth' type='i' direction='in'/>"
    "   <arg name='propertyNames' type='as' direction='in'/>"
    "   <arg name='revision' type='u' direction='out'/>"
    "   <arg name='layout' type='(ia{sv}av)' direction='out'/>"
    "  </method>"
    "  <method name='GetGroupProperties'>"
    "   <arg name='ids' type='ai' direction='in'/>"
    "   <arg name='propertyNames' type='as' direction='in'/>"
    "   <arg name='properties' type='a(ia{sv})' direction='out'/>"
    "  </method>"
    "  <method name='GetProperty'>"
    "   <arg name='id' type='i' direction='in'/>"
    "   <arg name='name' type='s' direction='in'/>"
    "   <arg name='value' type='v' direction='out'/>"
    "  </method>"
    "  <method name='Event'>"
    "   <arg name='id' type='i' direction='in'/>"
    "   <arg name='eventId' type='s' direction='in'/>"
    "   <arg name='data' type='v' direction='in'/>"
    "   <arg name='timestamp' type='u' direction='in'/>"
    "  </method>"
    "  <method name='EventGroup'>"
    "   <arg name='events' type='a(issv)' direction='in'/>"
    "   <arg name='idErrors' type='ai' direction='out'/>"
    "  </method>"
    "  <method name='AboutToShow'>"
    "   <arg name='id' type='i' direction='in'/>"
    "   <arg name='needUpdate' type='b' direction='out'/>"
    "  </method>"
    "  <method name='AboutToShowGroup'>"
    "   <arg name='ids' type='ai' direction='in'/>"
    "   <arg name='updatesNeeded' type='ai' direction='out'/>"
    "  </method>"
    "  <signal name='ItemsPropertiesUpdated'>"
    "   <arg name='updatedProps' type='a(ia{sv})'/>"
    "   <arg name='removedProps' type='a(ias)'/>"
    "  </signal>"
    "  <signal name='LayoutUpdated'>"
    "   <arg name='revision' type='u'/>"
    "   <arg name='parentId' type='i'/>"
    "  </signal>"
    "  <signal name='ItemActivationRequested'>"
    "   <arg name='id' type='i'/>"
    "   <arg name='timestamp' type='u'/>"
    "  </signal>"
    "  <property name='Version' type='u' access='read'/>"
    "  <property name='TextDirection' type='s' access='read'/>"
    "  <property name='Status' type='s' access='read'/>"
    " </interface>"
    "</node>";

static GDBusNodeInfo *
tray_item_get_sni_node_info(void)
{
  static GDBusNodeInfo *node_info = NULL;
  if (node_info == NULL) {
    GError *error = NULL;
    node_info = g_dbus_node_info_new_for_xml(sni_introspection_xml, &error);
    if (node_info == NULL) {
      g_warning("Failed to parse SNI introspection XML: %s",
                error ? error->message : "unknown error");
      g_clear_error(&error);
    }
  }
  return node_info;
}

static GDBusNodeInfo *
tray_item_get_menu_node_info(void)
{
  static GDBusNodeInfo *node_info = NULL;
  if (node_info == NULL) {
    GError *error = NULL;
    node_info = g_dbus_node_info_new_for_xml(menu_introspection_xml, &error);
    if (node_info == NULL) {
      g_warning("Failed to parse DBusMenu introspection XML: %s",
                error ? error->message : "unknown error");
      g_clear_error(&error);
    }
  }
  return node_info;
}

static gboolean
tray_state_has_task(AppState *state)
{
  if (state == NULL || state->store == NULL) {
    return FALSE;
  }

  return task_store_get_active(state->store) != NULL;
}

static const char *
tray_phase_action(PomodoroPhase phase)
{
  switch (phase) {
    case POMODORO_PHASE_SHORT_BREAK:
      return "Start Break";
    case POMODORO_PHASE_LONG_BREAK:
      return "Start Long Break";
    case POMODORO_PHASE_FOCUS:
    default:
      return "Start Focus";
  }
}

static const char *
tray_menu_toggle_label(TrayItem *tray)
{
  if (tray == NULL || tray->state == NULL || tray->state->timer == NULL) {
    return "Start Focus";
  }

  PomodoroTimerState run_state = pomodoro_timer_get_state(tray->state->timer);
  if (run_state == POMODORO_TIMER_RUNNING) {
    return "Pause";
  }
  if (run_state == POMODORO_TIMER_PAUSED) {
    return "Resume";
  }

  PomodoroPhase phase = pomodoro_timer_get_phase(tray->state->timer);
  return tray_phase_action(phase);
}

static const char *
tray_menu_overlay_label(TrayItem *tray)
{
  (void)tray;
  return "Toggle Floating Ball";
}

static gboolean
tray_menu_toggle_enabled(TrayItem *tray)
{
  if (tray == NULL || tray->state == NULL || tray->state->timer == NULL) {
    return FALSE;
  }

  return tray_state_has_task(tray->state);
}

static gboolean
tray_menu_skip_enabled(TrayItem *tray)
{
  if (tray == NULL || tray->state == NULL || tray->state->timer == NULL) {
    return FALSE;
  }

  PomodoroTimerState run_state = pomodoro_timer_get_state(tray->state->timer);
  return tray_state_has_task(tray->state) && run_state != POMODORO_TIMER_STOPPED;
}

static GVariant *
tray_menu_item_props(TrayItem *tray, gint id)
{
  GVariantBuilder props;
  g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));

  switch (id) {
    case MENU_ID_ROOT:
      g_variant_builder_add(&props,
                            "{sv}",
                            "children-display",
                            g_variant_new_string("submenu"));
      break;
    case MENU_ID_TOGGLE:
      g_variant_builder_add(&props,
                            "{sv}",
                            "label",
                            g_variant_new_string(tray_menu_toggle_label(tray)));
      g_variant_builder_add(&props,
                            "{sv}",
                            "enabled",
                            g_variant_new_boolean(tray_menu_toggle_enabled(tray)));
      break;
    case MENU_ID_SKIP:
      g_variant_builder_add(&props,
                            "{sv}",
                            "label",
                            g_variant_new_string("Skip"));
      g_variant_builder_add(&props,
                            "{sv}",
                            "enabled",
                            g_variant_new_boolean(tray_menu_skip_enabled(tray)));
      break;
    case MENU_ID_STOP:
      g_variant_builder_add(&props,
                            "{sv}",
                            "label",
                            g_variant_new_string("Stop"));
      g_variant_builder_add(&props,
                            "{sv}",
                            "enabled",
                            g_variant_new_boolean(tray_menu_skip_enabled(tray)));
      break;
    case MENU_ID_SEPARATOR:
      g_variant_builder_add(&props,
                            "{sv}",
                            "type",
                            g_variant_new_string("separator"));
      break;
    case MENU_ID_OVERLAY_TOGGLE:
      g_variant_builder_add(&props,
                            "{sv}",
                            "label",
                            g_variant_new_string(tray_menu_overlay_label(tray)));
      g_variant_builder_add(&props,
                            "{sv}",
                            "enabled",
                            g_variant_new_boolean(TRUE));
      break;
    case MENU_ID_OPEN_APP:
      g_variant_builder_add(&props,
                            "{sv}",
                            "label",
                            g_variant_new_string("Open App"));
      g_variant_builder_add(&props,
                            "{sv}",
                            "enabled",
                            g_variant_new_boolean(TRUE));
      break;
    case MENU_ID_QUIT:
      g_variant_builder_add(&props,
                            "{sv}",
                            "label",
                            g_variant_new_string("Quit"));
      g_variant_builder_add(&props,
                            "{sv}",
                            "enabled",
                            g_variant_new_boolean(TRUE));
      break;
    default:
      break;
  }

  g_variant_builder_add(&props,
                        "{sv}",
                        "visible",
                        g_variant_new_boolean(TRUE));

  return g_variant_builder_end(&props);
}

static GVariant *
tray_menu_build_item(TrayItem *tray, gint id, gboolean include_children)
{
  GVariant *props = tray_menu_item_props(tray, id);
  GVariantBuilder children;
  g_variant_builder_init(&children, G_VARIANT_TYPE("av"));

  if (include_children && id == MENU_ID_ROOT) {
    g_variant_builder_add(&children,
                          "v",
                          tray_menu_build_item(tray, MENU_ID_TOGGLE, FALSE));
    g_variant_builder_add(&children,
                          "v",
                          tray_menu_build_item(tray, MENU_ID_SKIP, FALSE));
    g_variant_builder_add(&children,
                          "v",
                          tray_menu_build_item(tray, MENU_ID_STOP, FALSE));
    g_variant_builder_add(&children,
                          "v",
                          tray_menu_build_item(tray, MENU_ID_SEPARATOR, FALSE));
    g_variant_builder_add(&children,
                          "v",
                          tray_menu_build_item(tray, MENU_ID_OVERLAY_TOGGLE, FALSE));
    g_variant_builder_add(&children,
                          "v",
                          tray_menu_build_item(tray, MENU_ID_OPEN_APP, FALSE));
    g_variant_builder_add(&children,
                          "v",
                          tray_menu_build_item(tray, MENU_ID_QUIT, FALSE));
  }

  return g_variant_new("(i@a{sv}@av)",
                       id,
                       props,
                       g_variant_builder_end(&children));
}

static GVariant *
tray_menu_build_layout(TrayItem *tray, gint parent_id, gint depth)
{
  gboolean include_children = depth != 0;

  if (parent_id != MENU_ID_ROOT) {
    include_children = FALSE;
  }

  return tray_menu_build_item(tray, parent_id, include_children);
}

static GVariant *
tray_icon_pixmap_empty(void)
{
  GVariantBuilder builder;
  g_variant_builder_init(&builder, G_VARIANT_TYPE("a(iiay)"));
  return g_variant_builder_end(&builder);
}

static GVariant *
tray_icon_pixmap_draw(int size)
{
  cairo_surface_t *surface =
      cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
  cairo_t *cr = cairo_create(surface);

  cairo_set_source_rgba(cr, 0, 0, 0, 0);
  cairo_paint(cr);

  double radius = (size / 2.0) - 2.0;
  double cx = size / 2.0;
  double cy = size / 2.0;

  cairo_set_line_width(cr, MAX(2.0, size * 0.14));
  cairo_set_source_rgba(cr, 0.06, 0.30, 0.36, 0.95);
  cairo_arc(cr, cx, cy, radius, 0, 2 * G_PI);
  cairo_stroke(cr);

  cairo_set_source_rgba(cr, 0.96, 0.92, 0.87, 0.95);
  cairo_arc(cr, cx, cy, radius * 0.38, 0, 2 * G_PI);
  cairo_fill(cr);

  cairo_destroy(cr);
  cairo_surface_flush(surface);

  int stride = cairo_image_surface_get_stride(surface);
  gsize len = (gsize)stride * (gsize)size;
  const guint8 *data = cairo_image_surface_get_data(surface);
  guint8 *copy = g_memdup2(data, len);
  cairo_surface_destroy(surface);

  GVariantBuilder bytes;
  g_variant_builder_init(&bytes, G_VARIANT_TYPE("ay"));
  for (gsize i = 0; i < len; i++) {
    g_variant_builder_add(&bytes, "y", copy[i]);
  }
  g_free(copy);

  GVariant *bytes_variant = g_variant_builder_end(&bytes);
  GVariantBuilder pixmap;
  g_variant_builder_init(&pixmap, G_VARIANT_TYPE("a(iiay)"));
  g_variant_builder_add(&pixmap, "(ii@ay)", size, size, bytes_variant);
  return g_variant_builder_end(&pixmap);
}

static GVariant *
tray_item_tooltip(TrayItem *tray)
{
  const char *title = APP_NAME;
  const char *text = "Focus timer ready.";

  if (tray != NULL && tray->state != NULL && tray->state->timer != NULL) {
    PomodoroTimerState state = pomodoro_timer_get_state(tray->state->timer);
    if (state == POMODORO_TIMER_RUNNING) {
      text = "Focus timer running.";
    } else if (state == POMODORO_TIMER_PAUSED) {
      text = "Focus timer paused.";
    }
  }

  GVariant *pixmap = (tray != NULL && tray->icon_pixmap != NULL)
                         ? g_variant_ref(tray->icon_pixmap)
                         : tray_icon_pixmap_empty();

  return g_variant_new("(s@a(iiay)ss)", tray_icon_name, pixmap, title, text);
}

static void
tray_action_present(AppState *state, GtkApplication *app)
{
  if (state != NULL && state->window != NULL) {
    gtk_window_present(state->window);
    return;
  }

  if (app != NULL) {
    g_application_activate(G_APPLICATION(app));
  }
}

static void
tray_action_toggle_timer(TrayItem *tray)
{
  if (tray == NULL || tray->state == NULL || tray->state->timer == NULL) {
    return;
  }

  if (!tray_state_has_task(tray->state)) {
    return;
  }

  pomodoro_timer_toggle(tray->state->timer);
  main_window_update_timer_ui(tray->state);
}

static void
tray_action_skip(TrayItem *tray)
{
  if (tray == NULL || tray->state == NULL || tray->state->timer == NULL) {
    return;
  }

  if (!tray_menu_skip_enabled(tray)) {
    return;
  }

  pomodoro_timer_skip(tray->state->timer);
  main_window_update_timer_ui(tray->state);
}

static void
tray_action_stop(TrayItem *tray)
{
  if (tray == NULL || tray->state == NULL || tray->state->timer == NULL) {
    return;
  }

  if (!tray_menu_skip_enabled(tray)) {
    return;
  }

  pomodoro_timer_stop(tray->state->timer);
  main_window_update_timer_ui(tray->state);
}

static void
tray_action_overlay_toggle(TrayItem *tray)
{
  if (tray == NULL || tray->state == NULL) {
    return;
  }

  overlay_window_toggle_visible(tray->state);
}

static void
tray_action_quit(TrayItem *tray)
{
  if (tray == NULL) {
    return;
  }

  if (tray->state != NULL) {
    tray->state->quit_requested = TRUE;
  }

  GtkApplication *app = tray->app;
  if (app == NULL && tray->state != NULL && tray->state->window != NULL) {
    app = gtk_window_get_application(tray->state->window);
  }

  if (app != NULL) {
    g_application_quit(G_APPLICATION(app));
  }
}

static void
tray_menu_handle_event(TrayItem *tray, gint id)
{
  switch (id) {
    case MENU_ID_TOGGLE:
      tray_action_toggle_timer(tray);
      break;
    case MENU_ID_SKIP:
      tray_action_skip(tray);
      break;
    case MENU_ID_STOP:
      tray_action_stop(tray);
      break;
    case MENU_ID_OVERLAY_TOGGLE:
      tray_action_overlay_toggle(tray);
      break;
    case MENU_ID_OPEN_APP:
      tray_action_present(tray ? tray->state : NULL, tray ? tray->app : NULL);
      break;
    case MENU_ID_QUIT:
      tray_action_quit(tray);
      break;
    default:
      break;
  }
}

static void
tray_item_emit_layout_updated(TrayItem *tray)
{
  if (tray == NULL || tray->connection == NULL) {
    return;
  }

  g_dbus_connection_emit_signal(tray->connection,
                                NULL,
                                MENU_OBJECT_PATH,
                                DBUSMENU_IFACE,
                                "LayoutUpdated",
                                g_variant_new("(ui)", tray->menu_revision, 0),
                                NULL);
}

static void
tray_item_emit_props_updated(TrayItem *tray)
{
  if (tray == NULL || tray->connection == NULL) {
    return;
  }

  GVariantBuilder updated;
  g_variant_builder_init(&updated, G_VARIANT_TYPE("a(ia{sv})"));
  g_variant_builder_add(&updated,
                        "(i@a{sv})",
                        MENU_ID_TOGGLE,
                        tray_menu_item_props(tray, MENU_ID_TOGGLE));
  g_variant_builder_add(&updated,
                        "(i@a{sv})",
                        MENU_ID_SKIP,
                        tray_menu_item_props(tray, MENU_ID_SKIP));
  g_variant_builder_add(&updated,
                        "(i@a{sv})",
                        MENU_ID_STOP,
                        tray_menu_item_props(tray, MENU_ID_STOP));
  g_variant_builder_add(&updated,
                        "(i@a{sv})",
                        MENU_ID_OVERLAY_TOGGLE,
                        tray_menu_item_props(tray, MENU_ID_OVERLAY_TOGGLE));

  GVariantBuilder removed;
  g_variant_builder_init(&removed, G_VARIANT_TYPE("a(ias)"));

  g_dbus_connection_emit_signal(tray->connection,
                                NULL,
                                MENU_OBJECT_PATH,
                                DBUSMENU_IFACE,
                                "ItemsPropertiesUpdated",
                                g_variant_new("(@a(ia{sv})@a(ias))",
                                              g_variant_builder_end(&updated),
                                              g_variant_builder_end(&removed)),
                                NULL);
}

static void
tray_register_on_watcher(TrayItem *tray)
{
  if (tray == NULL || tray->connection == NULL) {
    return;
  }

  g_dbus_connection_call(tray->connection,
                         SNI_WATCHER_NAME,
                         SNI_WATCHER_PATH,
                         SNI_WATCHER_IFACE,
                         "RegisterStatusNotifierItem",
                         g_variant_new("(s)", SNI_OBJECT_PATH),
                         NULL,
                         G_DBUS_CALL_FLAGS_NONE,
                         -1,
                         NULL,
                         NULL,
                         NULL);
}

static void
tray_watcher_appeared(GDBusConnection *connection,
                      const gchar *name,
                      const gchar *name_owner,
                      gpointer user_data)
{
  (void)connection;
  (void)name;
  (void)name_owner;

  tray_register_on_watcher((TrayItem *)user_data);
}

static void
tray_watcher_vanished(GDBusConnection *connection,
                      const gchar *name,
                      gpointer user_data)
{
  (void)connection;
  (void)name;
  (void)user_data;
}

static void
tray_sni_method_call(GDBusConnection *connection,
                     const gchar *sender,
                     const gchar *object_path,
                     const gchar *interface_name,
                     const gchar *method_name,
                     GVariant *parameters,
                     GDBusMethodInvocation *invocation,
                     gpointer user_data)
{
  (void)connection;
  (void)sender;
  (void)object_path;
  (void)interface_name;
  (void)parameters;

  TrayItem *tray = user_data;

  if (g_strcmp0(method_name, "Activate") == 0 ||
      g_strcmp0(method_name, "SecondaryActivate") == 0) {
    tray_action_present(tray ? tray->state : NULL, tray ? tray->app : NULL);
    g_dbus_method_invocation_return_value(invocation, NULL);
    return;
  }

  if (g_strcmp0(method_name, "ContextMenu") == 0) {
    g_dbus_method_invocation_return_value(invocation, NULL);
    return;
  }

  if (g_strcmp0(method_name, "Scroll") == 0) {
    g_dbus_method_invocation_return_value(invocation, NULL);
    return;
  }

  g_dbus_method_invocation_return_error(invocation,
                                        G_DBUS_ERROR,
                                        G_DBUS_ERROR_UNKNOWN_METHOD,
                                        "Unknown method: %s",
                                        method_name);
}

static GVariant *
tray_sni_get_property(GDBusConnection *connection,
                      const gchar *sender,
                      const gchar *object_path,
                      const gchar *interface_name,
                      const gchar *property_name,
                      GError **error,
                      gpointer user_data)
{
  (void)connection;
  (void)sender;
  (void)object_path;
  (void)interface_name;
  (void)error;

  TrayItem *tray = user_data;

  if (g_strcmp0(property_name, "Category") == 0) {
    return g_variant_new_string("ApplicationStatus");
  }
  if (g_strcmp0(property_name, "Id") == 0) {
    return g_variant_new_string("xfce4-floating-pomodoro");
  }
  if (g_strcmp0(property_name, "Title") == 0) {
    return g_variant_new_string(APP_NAME);
  }
  if (g_strcmp0(property_name, "Status") == 0) {
    return g_variant_new_string("Active");
  }
  if (g_strcmp0(property_name, "WindowId") == 0) {
    return g_variant_new_uint32(0);
  }
  if (g_strcmp0(property_name, "IconName") == 0) {
    return g_variant_new_string(tray_icon_name);
  }
  if (g_strcmp0(property_name, "IconPixmap") == 0) {
    if (tray != NULL && tray->icon_pixmap != NULL) {
      return g_variant_ref(tray->icon_pixmap);
    }
    return tray_icon_pixmap_empty();
  }
  if (g_strcmp0(property_name, "IconThemePath") == 0) {
    return g_variant_new_string("");
  }
  if (g_strcmp0(property_name, "OverlayIconName") == 0) {
    return g_variant_new_string("");
  }
  if (g_strcmp0(property_name, "OverlayIconPixmap") == 0) {
    return tray_icon_pixmap_empty();
  }
  if (g_strcmp0(property_name, "AttentionIconName") == 0) {
    return g_variant_new_string("");
  }
  if (g_strcmp0(property_name, "AttentionIconPixmap") == 0) {
    return tray_icon_pixmap_empty();
  }
  if (g_strcmp0(property_name, "AttentionMovieName") == 0) {
    return g_variant_new_string("");
  }
  if (g_strcmp0(property_name, "ToolTip") == 0) {
    return tray_item_tooltip(tray);
  }
  if (g_strcmp0(property_name, "ItemIsMenu") == 0) {
    return g_variant_new_boolean(FALSE);
  }
  if (g_strcmp0(property_name, "Menu") == 0) {
    return g_variant_new_object_path(MENU_OBJECT_PATH);
  }

  return NULL;
}

static void
tray_menu_method_call(GDBusConnection *connection,
                      const gchar *sender,
                      const gchar *object_path,
                      const gchar *interface_name,
                      const gchar *method_name,
                      GVariant *parameters,
                      GDBusMethodInvocation *invocation,
                      gpointer user_data)
{
  (void)connection;
  (void)sender;
  (void)object_path;
  (void)interface_name;

  TrayItem *tray = user_data;

  if (g_strcmp0(method_name, "GetLayout") == 0) {
    gint parent_id = 0;
    gint depth = -1;
    char **property_names = NULL;
    g_variant_get(parameters, "(ii^as)", &parent_id, &depth, &property_names);
    g_strfreev(property_names);
    GVariant *layout = tray_menu_build_layout(tray, parent_id, depth);
    g_dbus_method_invocation_return_value(
        invocation,
        g_variant_new("(u@(ia{sv}av))", tray->menu_revision, layout));
    return;
  }

  if (g_strcmp0(method_name, "GetGroupProperties") == 0) {
    GVariant *ids_variant = NULL;
    char **property_names = NULL;
    g_variant_get(parameters, "(@ai^as)", &ids_variant, &property_names);
    g_strfreev(property_names);

    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("a(ia{sv})"));

    if (ids_variant != NULL) {
      GVariantIter iter;
      gint id = 0;
      g_variant_iter_init(&iter, ids_variant);
      while (g_variant_iter_next(&iter, "i", &id)) {
        GVariant *props = tray_menu_item_props(tray, id);
        g_variant_builder_add(&builder, "(i@a{sv})", id, props);
      }
      g_variant_unref(ids_variant);
    }

    GVariant *props_variant = g_variant_builder_end(&builder);
    g_dbus_method_invocation_return_value(invocation,
                                          g_variant_new("(@a(ia{sv}))",
                                                        props_variant));
    return;
  }

  if (g_strcmp0(method_name, "GetProperty") == 0) {
    gint id = 0;
    const char *property = NULL;
    g_variant_get(parameters, "(is)", &id, &property);

    GVariant *props = tray_menu_item_props(tray, id);
    GVariant *value = NULL;
    if (props != NULL) {
      value = g_variant_lookup_value(props, property, NULL);
      g_variant_unref(props);
    }

    if (value == NULL) {
      value = g_variant_new_string("");
    }

    g_dbus_method_invocation_return_value(invocation,
                                          g_variant_new("(v)", value));
    return;
  }

  if (g_strcmp0(method_name, "Event") == 0) {
    gint id = 0;
    const char *event_id = NULL;
    GVariant *data = NULL;
    guint32 timestamp = 0;
    g_variant_get(parameters, "(isvu)", &id, &event_id, &data, &timestamp);
    if (data != NULL) {
      g_variant_unref(data);
    }
    (void)timestamp;
    if (event_id != NULL &&
        (g_strcmp0(event_id, "clicked") == 0 ||
         g_strcmp0(event_id, "activate") == 0)) {
      tray_menu_handle_event(tray, id);
    }
    g_dbus_method_invocation_return_value(invocation, NULL);
    return;
  }

  if (g_strcmp0(method_name, "EventGroup") == 0) {
    g_dbus_method_invocation_return_value(invocation,
                                          g_variant_new("(ai)", NULL));
    return;
  }

  if (g_strcmp0(method_name, "AboutToShow") == 0) {
    g_dbus_method_invocation_return_value(invocation,
                                          g_variant_new("(b)", FALSE));
    return;
  }

  if (g_strcmp0(method_name, "AboutToShowGroup") == 0) {
    g_dbus_method_invocation_return_value(invocation,
                                          g_variant_new("(ai)", NULL));
    return;
  }

  g_dbus_method_invocation_return_error(invocation,
                                        G_DBUS_ERROR,
                                        G_DBUS_ERROR_UNKNOWN_METHOD,
                                        "Unknown method: %s",
                                        method_name);
}

static GVariant *
tray_menu_get_property(GDBusConnection *connection,
                       const gchar *sender,
                       const gchar *object_path,
                       const gchar *interface_name,
                       const gchar *property_name,
                       GError **error,
                       gpointer user_data)
{
  (void)connection;
  (void)sender;
  (void)object_path;
  (void)interface_name;
  (void)error;
  (void)user_data;

  if (g_strcmp0(property_name, "Version") == 0) {
    return g_variant_new_uint32(1);
  }
  if (g_strcmp0(property_name, "TextDirection") == 0) {
    return g_variant_new_string("ltr");
  }
  if (g_strcmp0(property_name, "Status") == 0) {
    return g_variant_new_string("normal");
  }

  return NULL;
}

static const GDBusInterfaceVTable sni_vtable = {
    .method_call = tray_sni_method_call,
    .get_property = tray_sni_get_property,
    .set_property = NULL,
    .padding = {0}};

static const GDBusInterfaceVTable menu_vtable = {
    .method_call = tray_menu_method_call,
    .get_property = tray_menu_get_property,
    .set_property = NULL,
    .padding = {0}};

void
tray_item_create(GtkApplication *app, AppState *state)
{
  if (state == NULL || state->tray_item != NULL) {
    return;
  }

  TrayItem *tray = g_new0(TrayItem, 1);
  tray->state = state;
  tray->app = app;
  tray->menu_revision = 1;

  GError *error = NULL;
  tray->connection = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
  if (tray->connection == NULL) {
    g_warning("Failed to connect to session bus: %s",
              error ? error->message : "unknown error");
    g_clear_error(&error);
    g_free(tray);
    return;
  }

  tray->icon_pixmap = g_variant_ref_sink(tray_icon_pixmap_draw(22));

  GDBusNodeInfo *sni_info = tray_item_get_sni_node_info();
  if (sni_info == NULL || sni_info->interfaces[0] == NULL) {
    g_warning("Missing SNI introspection data");
  } else {
    tray->sni_registration_id =
        g_dbus_connection_register_object(tray->connection,
                                           SNI_OBJECT_PATH,
                                           sni_info->interfaces[0],
                                           &sni_vtable,
                                           tray,
                                           NULL,
                                           &error);
    if (tray->sni_registration_id == 0) {
      g_warning("Failed to register SNI object: %s",
                error ? error->message : "unknown error");
      g_clear_error(&error);
    }
  }

  GDBusNodeInfo *menu_info = tray_item_get_menu_node_info();
  if (menu_info == NULL || menu_info->interfaces[0] == NULL) {
    g_warning("Missing DBusMenu introspection data");
  } else {
    tray->menu_registration_id =
        g_dbus_connection_register_object(tray->connection,
                                           MENU_OBJECT_PATH,
                                           menu_info->interfaces[0],
                                           &menu_vtable,
                                           tray,
                                           NULL,
                                           &error);
    if (tray->menu_registration_id == 0) {
      g_warning("Failed to register DBusMenu object: %s",
                error ? error->message : "unknown error");
      g_clear_error(&error);
    }
  }

  tray->watcher_id = g_bus_watch_name(G_BUS_TYPE_SESSION,
                                      SNI_WATCHER_NAME,
                                      G_BUS_NAME_WATCHER_FLAGS_NONE,
                                      tray_watcher_appeared,
                                      tray_watcher_vanished,
                                      tray,
                                      NULL);

  state->tray_item = tray;
  tray_item_update(state);
}

void
tray_item_update(AppState *state)
{
  if (state == NULL || state->tray_item == NULL) {
    return;
  }

  TrayItem *tray = state->tray_item;
  if (state->timer == NULL) {
    return;
  }

  PomodoroTimerState run_state = pomodoro_timer_get_state(state->timer);
  PomodoroPhase phase = pomodoro_timer_get_phase(state->timer);
  gboolean has_task = tray_state_has_task(state);
  gboolean overlay_visible = overlay_window_is_visible(state);

  if (tray->has_state &&
      tray->last_timer_state == run_state &&
      tray->last_phase == phase &&
      tray->last_has_task == has_task &&
      tray->last_overlay_visible == overlay_visible) {
    return;
  }

  tray->has_state = TRUE;
  tray->last_timer_state = run_state;
  tray->last_phase = phase;
  tray->last_has_task = has_task;
  tray->last_overlay_visible = overlay_visible;
  tray->menu_revision++;
  tray_item_emit_props_updated(tray);
  tray_item_emit_layout_updated(tray);
}

void
tray_item_destroy(AppState *state)
{
  if (state == NULL || state->tray_item == NULL) {
    return;
  }

  TrayItem *tray = state->tray_item;
  state->tray_item = NULL;

  if (tray->watcher_id != 0) {
    g_bus_unwatch_name(tray->watcher_id);
    tray->watcher_id = 0;
  }

  if (tray->connection != NULL) {
    if (tray->sni_registration_id != 0) {
      g_dbus_connection_unregister_object(tray->connection,
                                          tray->sni_registration_id);
    }
    if (tray->menu_registration_id != 0) {
      g_dbus_connection_unregister_object(tray->connection,
                                          tray->menu_registration_id);
    }
    g_object_unref(tray->connection);
  }

  if (tray->icon_pixmap != NULL) {
    g_variant_unref(tray->icon_pixmap);
  }

  g_free(tray);
}
