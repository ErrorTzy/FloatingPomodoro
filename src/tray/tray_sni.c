#include "tray/tray_item_internal.h"

#include "config.h"

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

static GDBusNodeInfo *
tray_sni_get_node_info(void)
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

static GVariant *
tray_sni_tooltip(TrayItem *tray)
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
tray_register_on_watcher(TrayItem *tray)
{
  if (tray == NULL || tray->connection == NULL) {
    return;
  }

  g_dbus_connection_call(tray->connection,
                         TRAY_SNI_WATCHER_NAME,
                         TRAY_SNI_WATCHER_PATH,
                         TRAY_SNI_WATCHER_IFACE,
                         "RegisterStatusNotifierItem",
                         g_variant_new("(s)", TRAY_SNI_OBJECT_PATH),
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
    return tray_sni_tooltip(tray);
  }
  if (g_strcmp0(property_name, "ItemIsMenu") == 0) {
    return g_variant_new_boolean(FALSE);
  }
  if (g_strcmp0(property_name, "Menu") == 0) {
    return g_variant_new_object_path(TRAY_MENU_OBJECT_PATH);
  }

  return NULL;
}

static const GDBusInterfaceVTable sni_vtable = {
    .method_call = tray_sni_method_call,
    .get_property = tray_sni_get_property,
    .set_property = NULL,
    .padding = {0}};

void
tray_sni_register(TrayItem *tray)
{
  if (tray == NULL || tray->connection == NULL ||
      tray->sni_registration_id != 0) {
    return;
  }

  GDBusNodeInfo *sni_info = tray_sni_get_node_info();
  if (sni_info == NULL || sni_info->interfaces[0] == NULL) {
    g_warning("Missing SNI introspection data");
    return;
  }

  GError *error = NULL;
  tray->sni_registration_id =
      g_dbus_connection_register_object(tray->connection,
                                        TRAY_SNI_OBJECT_PATH,
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

void
tray_sni_unregister(TrayItem *tray)
{
  if (tray == NULL || tray->connection == NULL ||
      tray->sni_registration_id == 0) {
    return;
  }

  g_dbus_connection_unregister_object(tray->connection,
                                      tray->sni_registration_id);
  tray->sni_registration_id = 0;
}

void
tray_sni_watch(TrayItem *tray)
{
  if (tray == NULL || tray->watcher_id != 0) {
    return;
  }

  tray->watcher_id = g_bus_watch_name(G_BUS_TYPE_SESSION,
                                      TRAY_SNI_WATCHER_NAME,
                                      G_BUS_NAME_WATCHER_FLAGS_NONE,
                                      tray_watcher_appeared,
                                      tray_watcher_vanished,
                                      tray,
                                      NULL);
}

void
tray_sni_unwatch(TrayItem *tray)
{
  if (tray == NULL || tray->watcher_id == 0) {
    return;
  }

  g_bus_unwatch_name(tray->watcher_id);
  tray->watcher_id = 0;
}
