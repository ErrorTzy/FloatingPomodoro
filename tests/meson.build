if libsoup_dep.found() and json_glib_dep.found()
  test_deps = [
    dependency('glib-2.0'),
    dependency('gio-2.0'),
    libsoup_dep,
    json_glib_dep,
  ]

  test_sources = files(
    'integration_chrome_ollama.c',
    '../src/focus/chrome_cdp_client.c',
    '../src/focus/ollama_client.c',
  )

  integration_exe = executable(
    'integration_chrome_ollama',
    test_sources,
    dependencies: test_deps,
    include_directories: include_directories('..', '../src'),
  )

  test('integration_chrome_ollama',
    integration_exe,
    timeout: 300,
  )
else
  message('Skipping integration_chrome_ollama: libsoup/json-glib not available')
endif
