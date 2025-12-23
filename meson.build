project('xfce4-floating-pomodoro', 'c',
  version: '0.1.0',
  meson_version: '>=0.60.0',
  default_options: ['c_std=c11', 'warning_level=2']
)

gtk4_dep = dependency('gtk4', version: '>=4.8')

conf = configuration_data()
conf.set_quoted('APP_ID', 'com.scott.Xfce4FloatingPomodoro')
conf.set_quoted('APP_NAME', 'XFCE4 Floating Pomodoro')
conf.set_quoted('APP_VERSION', meson.project_version())

configure_file(
  output: 'config.h',
  configuration: conf
)

subdir('src')
