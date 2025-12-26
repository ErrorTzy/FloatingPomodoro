app_sources = files(
  'main.c',
  'app/app_init.c',
  'app/app_state.c',
  'core/pomodoro_timer.c',
  'core/task_store.c',
  'overlay/overlay_window.c',
  'overlay/overlay_window_actions.c',
  'overlay/overlay_window_draw.c',
  'overlay/overlay_window_ui.c',
  'storage/settings_storage.c',
  'storage/task_storage.c',
  'tray/tray_icon.c',
  'tray/tray_item.c',
  'tray/tray_menu.c',
  'tray/tray_sni.c',
  'ui/dialogs.c',
  'ui/main_window.c',
  'ui/task_list.c',
  'utils/x11.c',
) + resources

executable(
  'xfce4-floating-pomodoro',
  app_sources,
  dependencies: [gtk4_dep, fontconfig_dep, x11_dep],
  include_directories: include_directories('..', '.'),
  install: true
)
