#include "tray/tray_item_internal.h"

#include "overlay/overlay_window.h"
#include "ui/main_window.h"

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
tray_menu_get_node_info(void)
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

  return tray_item_has_task(tray->state);
}

static gboolean
tray_menu_skip_enabled(TrayItem *tray)
{
  if (tray == NULL || tray->state == NULL || tray->state->timer == NULL) {
    return FALSE;
  }

  PomodoroTimerState run_state = pomodoro_timer_get_state(tray->state->timer);
  return tray_item_has_task(tray->state) && run_state != POMODORO_TIMER_STOPPED;
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
                          tray_menu_build_item(tray,
                                               MENU_ID_OVERLAY_TOGGLE,
                                               FALSE));
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

static void
tray_action_toggle_timer(TrayItem *tray)
{
  if (tray == NULL || tray->state == NULL || tray->state->timer == NULL) {
    return;
  }

  if (!tray_item_has_task(tray->state)) {
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

void
tray_menu_emit_layout_updated(TrayItem *tray)
{
  if (tray == NULL || tray->connection == NULL) {
    return;
  }

  g_dbus_connection_emit_signal(tray->connection,
                                NULL,
                                TRAY_MENU_OBJECT_PATH,
                                TRAY_DBUSMENU_IFACE,
                                "LayoutUpdated",
                                g_variant_new("(ui)", tray->menu_revision, 0),
                                NULL);
}

void
tray_menu_emit_props_updated(TrayItem *tray)
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
                                TRAY_MENU_OBJECT_PATH,
                                TRAY_DBUSMENU_IFACE,
                                "ItemsPropertiesUpdated",
                                g_variant_new("(@a(ia{sv})@a(ias))",
                                              g_variant_builder_end(&updated),
                                              g_variant_builder_end(&removed)),
                                NULL);
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

static const GDBusInterfaceVTable menu_vtable = {
    .method_call = tray_menu_method_call,
    .get_property = tray_menu_get_property,
    .set_property = NULL,
    .padding = {0}};

void
tray_menu_register(TrayItem *tray)
{
  if (tray == NULL || tray->connection == NULL ||
      tray->menu_registration_id != 0) {
    return;
  }

  GDBusNodeInfo *menu_info = tray_menu_get_node_info();
  if (menu_info == NULL || menu_info->interfaces[0] == NULL) {
    g_warning("Missing DBusMenu introspection data");
    return;
  }

  GError *error = NULL;
  tray->menu_registration_id =
      g_dbus_connection_register_object(tray->connection,
                                        TRAY_MENU_OBJECT_PATH,
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

void
tray_menu_unregister(TrayItem *tray)
{
  if (tray == NULL || tray->connection == NULL ||
      tray->menu_registration_id == 0) {
    return;
  }

  g_dbus_connection_unregister_object(tray->connection,
                                      tray->menu_registration_id);
  tray->menu_registration_id = 0;
}
