project('floating-pomodoro', 'c',
  version: '0.1.0',
  meson_version: '>=0.60.0',
  default_options: ['c_std=c11', 'warning_level=2']
)

gtk4_dep = dependency('gtk4', version: '>=4.8')
fontconfig_dep = dependency('fontconfig')
x11_dep = dependency('x11', required: false)
sqlite3_dep = dependency('sqlite3')
chrome_ollama_opt = get_option('chrome_ollama')
chrome_ollama_enabled = false
if chrome_ollama_opt.enabled()
  libsoup_dep = dependency('libsoup-3.0', required: true)
  json_glib_dep = dependency('json-glib-1.0', required: true)
  chrome_ollama_enabled = true
elif chrome_ollama_opt.auto()
  libsoup_dep = dependency('libsoup-3.0', required: false)
  json_glib_dep = dependency('json-glib-1.0', required: false)
  chrome_ollama_enabled = libsoup_dep.found() and json_glib_dep.found()
else
  libsoup_dep = dependency('libsoup-3.0', required: false)
  json_glib_dep = dependency('json-glib-1.0', required: false)
endif
gnome = import('gnome')

conf = configuration_data()
conf.set_quoted('APP_ID', 'com.scott.Xfce4FloatingPomodoro')
conf.set_quoted('APP_NAME', 'Floating Pomodoro')
conf.set_quoted('APP_VERSION', meson.project_version())
conf.set10('HAVE_CHROME_OLLAMA', chrome_ollama_enabled)
conf.set10('HAVE_LIBSOUP', libsoup_dep.found())
conf.set10('HAVE_JSON_GLIB', json_glib_dep.found())

configure_file(
  output: 'config.h',
  configuration: conf
)

resources = gnome.compile_resources(
  'floating_pomodoro_resources',
  'data/resources.gresource.xml',
  source_dir: 'data',
  c_name: 'floating_pomodoro'
)

subdir('src')
subdir('tests')
