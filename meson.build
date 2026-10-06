project('clinic_server', 'cpp',
  version : '1.0.0',
  default_options : ['warning_level=2', 'cpp_std=c++17', 'default_library=static']
)

sqlite3_dep = dependency('sqlite3', required : true)
threads_dep = dependency('threads', required : true)
json_dep = dependency('nlohmann_json', fallback : ['nlohmann_json', 'nlohmann_json_dep'], required : true)
httplib_dep = dependency('cpp-httplib', required : false)

cpp = meson.get_compiler('cpp')
win_deps = []
if host_machine.system() == 'windows'
  win_deps = [
    cpp.find_library('ws2_32', required : true),
    cpp.find_library('crypt32', required : true),
  ]
endif

server_deps = [sqlite3_dep, threads_dep] + win_deps
if json_dep.found()
  server_deps += json_dep
endif
if httplib_dep.found()
  server_deps += httplib_dep
endif

inc = include_directories('include')

server_sources = files(
  'src/db.cpp',
  'src/cache.cpp',
  'src/server.cpp'
)

# Main Server Executable
executable('clinic_server',
  files('src/main.cpp') + server_sources,
  include_directories : inc,
  dependencies : server_deps,
  install : true
)

# Test Suite Executable
test_deps = [sqlite3_dep, threads_dep] + win_deps
if json_dep.found()
  test_deps += json_dep
endif

test_server_exec = executable('clinic_server_test',
  files('tests/test_server.cpp', 'src/db.cpp', 'src/cache.cpp'),
  include_directories : inc,
  dependencies : test_deps
)

test('server_tests', test_server_exec)
