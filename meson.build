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

# Modules split into .h/.cpp pairs (own translation unit), shared between the
# main executable and the test binary. Modules still #include-d as header-only
# .hpp files don't need an entry here.
# Core sources needed by both the main executable and the test binary
# (test/core/*.cpp exercises these directly, no extra dependencies beyond
# what kokusei_test already links).
kokusei_core_split_sources = [
  'src/core/log.cpp',
  'src/core/poll_source.cpp',
  'src/core/deferred_call.cpp',
  'src/core/async_process.cpp',
]

# Render sources needed by both: only the ones test/render/*.cpp actually
# exercises. The rest (text/icon/panel_chrome/etc., which pull in
# cairo/pango/freetype) are main-executable-only, added below.
kokusei_render_test_sources = [
  'src/render/animation.cpp',
  'src/render/texture.cpp',
  'src/render/image.cpp',
]

# Render sources needed by the main executable only (cairo/pango/freetype-
# dependent, not exercised by any test today).
kokusei_render_main_only_sources = [
  'src/render/gl.cpp',
  'src/render/renderer.cpp',
  'src/render/node.cpp',
  'src/render/text.cpp',
  'src/render/icon.cpp',
  'src/render/texture_cache.cpp',
  'src/render/panel_scroll.cpp',
  'src/render/text_field.cpp',
  'src/render/panel_chrome.cpp',
  'src/render/overlay_panel.cpp',
]

# D-Bus sources needed by both: only the pure-parsing ones test/dbus/*.cpp
# exercises. The live D-Bus services (bluetooth/network/tray/upower) are
# main-executable-only, added below.
kokusei_dbus_test_sources = [
  'src/dbus/bluetooth/bluetooth_types.cpp',
  'src/dbus/network/network_types.cpp',
]

kokusei_dbus_main_only_sources = [
  'src/dbus/bluetooth/bluetooth_service.cpp',
  'src/dbus/network/network_service.cpp',
  'src/dbus/tray/tray_service.cpp',
  'src/dbus/upower.cpp',
]

# system/rfkill.cpp is exercised directly by test/system/test_rfkill.cpp;
# system/pipewire.cpp links libpipewire, main-executable-only.
kokusei_system_test_sources = [
  'src/system/rfkill.cpp',
]

kokusei_system_main_only_sources = [
  'src/system/pipewire.cpp',
]

# All launcher pure-logic sources are exercised by test/launcher/*.cpp.
kokusei_launcher_test_sources = [
  'src/launcher/spawn.cpp',
  'src/launcher/visit_store.cpp',
  'src/launcher/icon_theme.cpp',
  'src/launcher/desktop_entry.cpp',
  'src/launcher/apps_provider.cpp',
  'src/launcher/files_provider.cpp',
  'src/launcher/search.cpp',
  'src/launcher/submenu.cpp',
  'src/launcher/launch_action.cpp',
]

# launcher_state.cpp/launcher_draw.cpp touch EGL/GL directly, no test coverage.
kokusei_launcher_main_only_sources = [
  'src/launcher/launcher_state.cpp',
  'src/launcher/launcher_draw.cpp',
]

# bar/panels/**: real EGL/GL rendering, no test coverage, main-executable-only.
kokusei_bar_panels_main_only_sources = [
  'src/bar/panels/tray_panel/tray_panel_state.cpp',
  'src/bar/panels/tray_panel/tray_panel.cpp',
  'src/bar/panels/tray_panel/tray_menu_state.cpp',
  'src/bar/panels/tray_panel/tray_menu.cpp',
  'src/bar/panels/network_panel/network_panel_state.cpp',
  'src/bar/panels/network_panel/network_panel.cpp',
  'src/bar/panels/bluetooth_panel/bluetooth_panel_state.cpp',
  'src/bar/panels/bluetooth_panel/bluetooth_panel.cpp',
  'src/bar/panels/volume_panel/volume_panel_state.cpp',
  'src/bar/panels/volume_panel/volume_panel.cpp',
]

# bar/ itself + widgets/: real EGL/GL rendering, no test coverage,
# main-executable-only.
kokusei_bar_main_only_sources = [
  'src/bar/bar.cpp',
  'src/bar/bar_autohide.cpp',
  'src/bar/widgets/widget_capsule.cpp',
  'src/bar/widgets/clock_widget.cpp',
  'src/bar/widgets/workspace_widget.cpp',
  'src/bar/widgets/battery_widget.cpp',
  'src/bar/widgets/power_widget.cpp',
  'src/bar/widgets/bluetooth_widget.cpp',
  'src/bar/widgets/network_widget.cpp',
  'src/bar/widgets/control_center_widget.cpp',
  'src/bar/widgets/volume_widget.cpp',
]

# wayland/keyboard.cpp is exercised by test/wayland/test_keyboard.cpp;
# wayland/pointer.cpp is dragged in with it (keyboard_attach_seat calls into
# it). The rest (idle/frame_clock/output_scale/layer_surface/hyprland/shojiwm)
# have no test coverage - layer_surface.cpp in particular needs the
# wlr-layer-shell generated protocol code, which is only added to the main
# executable's sources.
kokusei_wayland_test_sources = [
  'src/wayland/keyboard.cpp',
  'src/wayland/pointer.cpp',
]

kokusei_wayland_main_only_sources = [
  'src/idle/idle.cpp',
  'src/wayland/frame_clock.cpp',
  'src/wayland/output_scale.cpp',
  'src/wayland/layer_surface.cpp',
  'src/wayland/hyprland.cpp',
  'src/wayland/shojiwm.cpp',
]

# osd/, wallpaper/, notification/: real EGL/GL rendering, no test coverage,
# main-executable-only.
kokusei_overlay_main_only_sources = [
  'src/osd/osd.cpp',
  'src/wallpaper/wallpaper.cpp',
  'src/wallpaper/wallpaper_cache.cpp',
  'src/notification/notification_state.cpp',
  'src/notification/notification_draw.cpp',
  'src/logout/logout_state.cpp',
  'src/logout/logout_draw.cpp',
]

# app/config.cpp is exercised by test/app/test_config.cpp and
# test/app/test_wallpaper_resolve.cpp; single_instance_lock.cpp has no test.
kokusei_app_test_sources = [
  'src/app/config.cpp',
]

kokusei_app_main_only_sources = [
  'src/app/single_instance_lock.cpp',
  'src/app/ipc.cpp',
]

# settings/wallpaper_picker.cpp (wallpaper_picker_less, tested) is split from
# wallpaper_picker_thumbnail.cpp (wallpaper_picker_request_thumbnail, needs
# wallpaper.h's EGL surface handles - untested, and would otherwise drag the
# whole EGL/wayland-egl/layer-shell dependency chain into the test binary
# just to link one unused function).
kokusei_settings_test_sources = [
  'src/settings/wallpaper_picker.cpp',
]

kokusei_settings_main_only_sources = [
  'src/settings/wallpaper_picker_thumbnail.cpp',
  'src/settings/settings_state.cpp',
  'src/settings/settings_draw.cpp',
]

kokusei_split_sources = kokusei_core_split_sources + kokusei_render_test_sources + kokusei_render_main_only_sources + kokusei_dbus_test_sources + kokusei_dbus_main_only_sources + kokusei_system_test_sources + kokusei_system_main_only_sources + kokusei_launcher_test_sources + kokusei_launcher_main_only_sources + kokusei_wayland_test_sources + kokusei_wayland_main_only_sources + kokusei_overlay_main_only_sources + kokusei_app_test_sources + kokusei_app_main_only_sources + kokusei_settings_test_sources + kokusei_settings_main_only_sources + kokusei_bar_panels_main_only_sources + kokusei_bar_main_only_sources
kokusei_test_split_sources = kokusei_core_split_sources + kokusei_render_test_sources + kokusei_dbus_test_sources + kokusei_system_test_sources + kokusei_launcher_test_sources + kokusei_wayland_test_sources + kokusei_app_test_sources + kokusei_settings_test_sources

executable('kokusei','src/kokusei.cpp', kokusei_split_sources,
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
  'test/kokusei-test.cpp', kokusei_test_split_sources,
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
