app_sources = files(
  'main.c',
  'app/app_init.c',
  'app/app_state.c',
  'core/pomodoro_timer.c',
  'core/task_store.c',
  'focus/chrome_cdp_client.c',
  'focus/focus_guard.c',
  'focus/focus_guard_config.c',
  'focus/focus_guard_x11.c',
  'focus/ollama_client.c',
  'focus/trafilatura_client.c',
  'overlay/overlay_window.c',
  'overlay/overlay_window_actions.c',
  'overlay/overlay_window_draw.c',
  'overlay/overlay_window_ui.c',
  'storage/settings_storage.c',
  'storage/task_storage.c',
  'storage/usage_stats_storage.c',
  'tray/tray_icon.c',
  'tray/tray_item.c',
  'tray/tray_menu.c',
  'tray/tray_sni.c',
  'ui/dialogs_archive_settings.c',
  'ui/dialogs_archived.c',
  'ui/dialogs_confirm.c',
  'ui/dialogs_focus_guard.c',
  'ui/dialogs_timer_settings.c',
  'ui/focus_guard_settings_model.c',
  'ui/main_window.c',
  'ui/task_list.c',
  'utils/autostart.c',
  'utils/x11.c',
) + resources

app_deps = [
  gtk4_dep,
  fontconfig_dep,
  x11_dep,
  sqlite3_dep,
]

if libsoup_dep.found() and json_glib_dep.found()
  app_deps += [libsoup_dep, json_glib_dep]
endif

executable(
  'xfce4-floating-pomodoro',
  app_sources,
  dependencies: app_deps,
  include_directories: include_directories('..', '.'),
  install: true
)
