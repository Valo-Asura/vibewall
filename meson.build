project(
  'vibewallREzero',
  'c',
  'cpp',
  version: '0.1.0',
  default_options: [
    'cpp_std=c++23',
    'warning_level=2',
    'werror=false',
  ],
)

cpp = meson.get_compiler('cpp')
fs = import('fs')

wayland_client = dependency('wayland-client')
wayland_egl = dependency('wayland-egl')
egl = dependency('egl')
glesv2 = dependency('glesv2')
libepoxy = dependency('epoxy')
xkbcommon = dependency('xkbcommon')
sqlite3 = dependency('sqlite3')
libcurl = dependency('libcurl')
vips_cpp = dependency('vips-cpp')
tomlplusplus = dependency('tomlplusplus')
nlohmann_json = dependency('nlohmann_json')
threads = dependency('threads')

scanner = find_program('wayland-scanner')
wlr_protocols = dependency('wlr-protocols')
wlr_protocol_dir = wlr_protocols.get_variable(pkgconfig: 'pkgdatadir')
layer_xml = wlr_protocol_dir / 'unstable/wlr-layer-shell-unstable-v1.xml'
wayland_protocols = dependency('wayland-protocols')
wayland_protocol_dir = wayland_protocols.get_variable(pkgconfig: 'pkgdatadir')
xdg_shell_xml = wayland_protocol_dir / 'stable/xdg-shell/xdg-shell.xml'

layer_client_header = custom_target(
  'wlr-layer-shell-client-header',
  input: layer_xml,
  output: 'wlr-layer-shell-unstable-v1-client-protocol.h',
  command: [scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
)

layer_client_code = custom_target(
  'wlr-layer-shell-client-code',
  input: layer_xml,
  output: 'wlr-layer-shell-unstable-v1-protocol.c',
  command: [scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
)

xdg_client_header = custom_target(
  'xdg-shell-client-header',
  input: xdg_shell_xml,
  output: 'xdg-shell-client-protocol.h',
  command: [scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
)

xdg_client_code = custom_target(
  'xdg-shell-client-code',
  input: xdg_shell_xml,
  output: 'xdg-shell-protocol.c',
  command: [scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
)

generated_protocol_dep = declare_dependency(
  sources: [layer_client_header, layer_client_code, xdg_client_header, xdg_client_code],
  include_directories: include_directories('.'),
)

inc = include_directories('src')

core_light = files(
  'src/core/app_paths.cpp',
  'src/core/color_sort.cpp',
  'src/core/config.cpp',
  'src/core/database.cpp',
  'src/core/filters.cpp',
  'src/core/model.cpp',
  'src/core/process.cpp',
  'src/core/wallpaper_apply.cpp',
)

core_heavy = files(
  'src/core/scanner.cpp',
  'src/core/thumbnail_cache.cpp',
  'src/core/video_thumbnail.cpp',
  'src/core/wallhaven_client.cpp',
)

executable(
  'vibewall',
  ['src/cli/main.cpp', core_light, core_heavy],
  include_directories: inc,
  dependencies: [sqlite3, libcurl, vips_cpp, tomlplusplus, nlohmann_json, threads],
  install: true,
)

executable(
  'vibewall-daemon',
  ['src/daemon/main.cpp', core_light],
  include_directories: inc,
  dependencies: [sqlite3, tomlplusplus, threads],
  install: true,
)

executable(
  'vibewall-picker',
  [
    'src/picker/main.cpp',
    'src/picker/renderer.cpp',
    'src/picker/wayland_app.cpp',
    core_light,
    'src/core/wallhaven_client.cpp',
  ],
  include_directories: inc,
  dependencies: [
    wayland_client,
    wayland_egl,
    egl,
    glesv2,
    libepoxy,
    xkbcommon,
    sqlite3,
    libcurl,
    vips_cpp,
    tomlplusplus,
    nlohmann_json,
    threads,
    generated_protocol_dep,
  ],
  install: true,
)

install_data('config/default.toml', install_dir: get_option('datadir') / 'vibewallREzero')
install_data('systemd/vibewallrezero-daemon.service', install_dir: get_option('datadir') / 'systemd/user')
install_data(
  'scripts/benchmark.sh',
  install_dir: get_option('datadir') / 'vibewallREzero/scripts',
  install_mode: 'rwxr-xr-x',
)

test_core = executable(
  'test_core',
  ['tests/test_core.cpp', core_light],
  include_directories: inc,
  dependencies: [sqlite3, tomlplusplus, threads],
)
test('core', test_core)

test_wallhaven = executable(
  'test_wallhaven',
  ['tests/test_wallhaven.cpp', 'src/core/wallhaven_client.cpp', core_light],
  include_directories: inc,
  dependencies: [libcurl, nlohmann_json, sqlite3, tomlplusplus, threads],
)
test('wallhaven-json', test_wallhaven)
