app_sources = files(
  'main.c',
  'core/task_store.c',
  'storage/task_storage.c',
) + resources

executable(
  'xfce4-floating-pomodoro',
  app_sources,
  dependencies: [gtk4_dep, fontconfig_dep],
  include_directories: include_directories('..'),
  install: true
)
