project('kokusei', 'c', 'cpp',
  version : '0.1.0',
  default_options : ['cpp_std=c++23', 'warning_level=3'])

wayland_client_dep = dependency('wayland-client')
wayland_egl_dep    = dependency('wayland-egl')
egl_dep            = dependency('egl')
glesv2_dep         = dependency('glesv2')
tomlplusplus_dep   = dependency('tomlplusplus')
cairo_dep          = dependency('cairo')
pangocairo_dep     = dependency('pangocairo')
nlohmann_json_dep  = dependency('nlohmann_json')
sdbus_cpp_dep      = dependency('sdbus-c++')
freetype2_dep      = dependency('freetype2')
libpipewire_dep    = dependency('libpipewire-0.3')
xkbcommon_dep      = dependency('xkbcommon')
libpng_dep         = dependency('libpng')
libjpeg_dep        = dependency('libjpeg')

# Tabler Icons font, required for shell-wide icons
kokusei_font_dir = get_option('prefix') / get_option('datadir') / 'kokusei' / 'fonts'
install_data('assets/fonts/tabler-icons.ttf', install_dir: kokusei_font_dir)

# Yuji Mai font, required for logout module
install_data('assets/fonts/YujiMai.ttf', install_dir: kokusei_font_dir)

# Default wallpaper
kokusei_data_dir = get_option('prefix') / get_option('datadir') / 'kokusei'
install_data('assets/default.png', install_dir: kokusei_data_dir)

# Keqing constellations
kokusei_bullet_dir = kokusei_data_dir / 'bullets'
foreach n : ['1', '2', '3', '4', '5', '6']
  install_data('assets/bullets/' + n + '.png', install_dir: kokusei_bullet_dir)
endforeach

# Logout mascot logo
install_data('assets/logo.png', install_dir: kokusei_data_dir)

wayland_scanner = find_program('wayland-scanner')
wayland_protocols_dir = dependency('wayland-protocols').get_variable(pkgconfig: 'pkgdatadir')

# wlr-layer-shell's get_popup request references xdg_popup, so xdg-shell's
# generated code has to be linked in too even though nothing here uses it directly.
xdg_shell_xml = wayland_protocols_dir / 'stable/xdg-shell/xdg-shell.xml'

xdg_shell_header = custom_target('xdg-shell-client-header',
  input:   xdg_shell_xml,
  output:  'xdg-shell-client-protocol.h',
  command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'])

xdg_shell_source = custom_target('xdg-shell-client-source',
  input:   xdg_shell_xml,
  output:  'xdg-shell-client-protocol.c',
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'])

# 'namespace' is a C++ keyword; the protocol uses it as an argument name, so
# rename it to 'name_space' in the generated code (same fix noctalia uses).
layer_shell_header = custom_target('wlr-layer-shell-client-header',
  input:   'protocols/wlr-layer-shell-unstable-v1.xml',
  output:  'wlr-layer-shell-unstable-v1-client-protocol.h',
  command: ['sh', '-c', '"$1" client-header "$2" "$3" && sed -i s/namespace/name_space/g "$3"',
             '_', wayland_scanner.full_path(), '@INPUT@', '@OUTPUT@'])

layer_shell_source = custom_target('wlr-layer-shell-client-source',
  input:   'protocols/wlr-layer-shell-unstable-v1.xml',
  output:  'wlr-layer-shell-unstable-v1-client-protocol.c',
  command: ['sh', '-c', '"$1" private-code "$2" "$3" && sed -i s/namespace/name_space/g "$3"',
             '_', wayland_scanner.full_path(), '@INPUT@', '@OUTPUT@'])

# idle module protocols: ext-idle-notify-v1 (staging, staged timeout
# notifications) + idle-inhibit-unstable-v1 (manual inhibit). Both are plain
# upstream protocols (no C++-keyword clashes like layer-shell's), so no sed fixup needed.
ext_idle_notify_xml = wayland_protocols_dir / 'staging/ext-idle-notify/ext-idle-notify-v1.xml'
idle_inhibit_xml    = wayland_protocols_dir / 'unstable/idle-inhibit/idle-inhibit-unstable-v1.xml'

idle_protocol_headers = []
idle_protocol_sources = []
foreach proto : [['ext-idle-notify-v1', ext_idle_notify_xml], ['idle-inhibit-unstable-v1', idle_inhibit_xml]]
  proto_name = proto[0]
  proto_xml = proto[1]
  proto_header = custom_target(proto_name + '-client-header',
    input:   proto_xml,
    output:  proto_name + '-client-protocol.h',
    command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'])
  proto_source = custom_target(proto_name + '-client-source',
    input:   proto_xml,
    output:  proto_name + '-client-protocol.c',
    command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'])
  idle_protocol_headers += proto_header
  idle_protocol_sources += proto_source
endforeach

threads_dep = dependency('threads')

executable('kokusei', 'src/kokusei.cpp',
  layer_shell_header, layer_shell_source, xdg_shell_header, xdg_shell_source,
  idle_protocol_headers, idle_protocol_sources,
  dependencies: [wayland_client_dep, wayland_egl_dep, egl_dep, glesv2_dep, tomlplusplus_dep,
                 cairo_dep, pangocairo_dep, nlohmann_json_dep, sdbus_cpp_dep, freetype2_dep,
                 libpipewire_dep, xkbcommon_dep, threads_dep,
                 libpng_dep, libjpeg_dep],
  cpp_args: ['-DKOKUSEI_FONT_DIR="' + kokusei_font_dir + '"',
             '-DKOKUSEI_DEFAULT_WALLPAPER="' + kokusei_data_dir / 'default.png' + '"',
             '-DKOKUSEI_BULLET_DIR="' + kokusei_bullet_dir + '"',
             '-DKOKUSEI_YUJIMAI_FONT="' + kokusei_font_dir / 'YujiMai.ttf' + '"',
             '-DKOKUSEI_LOGOUT_LOGO="' + kokusei_data_dir / 'logo.png' + '"'],
  install: true, install_dir: 'bin')

kokusei_test = executable('kokusei-test',
  'test/kokusei-test.cpp',
  'test/app/test_config.cpp',
  'test/app/test_wallpaper_resolve.cpp',
  'test/app/test_wallpaper_picker_sort.cpp',
  'test/core/test_async_process.cpp',
  'test/core/test_deferred_call.cpp',
  'test/core/test_poll_source.cpp',
  'test/dbus/test_network_parse.cpp',
  'test/dbus/test_bluetooth.cpp',
  'test/launcher/test_spawn_helpers.cpp',
  'test/launcher/test_desktop_entry.cpp',
  'test/launcher/test_visit_store.cpp',
  'test/launcher/test_apps_provider.cpp',
  'test/launcher/test_files_provider.cpp',
  'test/launcher/test_search.cpp',
  'test/launcher/test_submenu.cpp',
  'test/launcher/test_launch_action.cpp',
  'test/launcher/test_icon_theme.cpp',
  'test/wayland/test_keyboard.cpp',
  'test/system/test_rfkill.cpp',
  'test/render/test_animation.cpp',
  'test/render/test_palette.cpp',
  'test/render/test_image_decode.cpp',
  dependencies: [threads_dep, tomlplusplus_dep, xkbcommon_dep, wayland_client_dep, sdbus_cpp_dep,
                 libpng_dep, libjpeg_dep, glesv2_dep],
  cpp_args: ['-DKOKUSEI_DEFAULT_WALLPAPER="' + kokusei_data_dir / 'default.png' + '"'])
test('kokusei', kokusei_test)
