app_sources = files('main.c') + resources

executable(
  'xfce4-floating-pomodoro',
  app_sources,
  dependencies: [gtk4_dep],
  include_directories: include_directories('..'),
  install: true
)
