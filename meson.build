project(
  'kokusei',
  'c',
  'cpp',
  version: '0.1.0',
  default_options: ['cpp_std=c++23', 'warning_level=3'],
)

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
libpipewire_dep    = dependency('libpipewire-0.3', include_type: 'system')
xkbcommon_dep      = dependency('xkbcommon')
libpng_dep         = dependency('libpng')
libjpeg_dep        = dependency('libjpeg')
threads_dep        = dependency('threads')
libavformat_dep    = dependency('libavformat')
libavcodec_dep     = dependency('libavcodec')
libavfilter_dep    = dependency('libavfilter')
libavutil_dep      = dependency('libavutil')
# Zero-copy VAAPI wallpaper decode (render/video_texture.cpp,
# service/wallpaper_hw_decode.cpp): DRM_FORMAT_* fourcc constants for the
# dma-buf plane import, nothing else - the VAAPI calls themselves stay
# inside FFmpeg's own hwcontext_vaapi (already linked via libavutil), so
# libva/libva-drm aren't needed directly.
libdrm_dep         = dependency('libdrm')

kokusei_inc = include_directories('src')

# Tabler Icons font, required for shell-wide icons
kokusei_font_dir = get_option('prefix') / get_option('datadir') / 'kokusei' / 'fonts'
install_data('assets/fonts/tabler-icons.ttf', install_dir: kokusei_font_dir)

# Yuji Mai font, required for starward module
install_data('assets/fonts/YujiMai.ttf', install_dir: kokusei_font_dir)

# Default wallpaper
kokusei_data_dir = get_option('prefix') / get_option('datadir') / 'kokusei'
install_data('assets/default.png', install_dir: kokusei_data_dir)

# Keqing constellations
kokusei_bullet_dir = kokusei_data_dir / 'bullets'
foreach n : ['1', '2', '3', '4', '5', '6']
  install_data('assets/bullets/' + n + '.png', install_dir: kokusei_bullet_dir)
endforeach

# Starward mascot logo
install_data('assets/logo.png', install_dir: kokusei_data_dir)

wayland_scanner = find_program('wayland-scanner')
wayland_protocols_dir = dependency('wayland-protocols').get_variable(pkgconfig: 'pkgdatadir')

# wlr-layer-shell's get_popup request references xdg_popup, so xdg-shell's
# generated code has to be linked in too even though nothing here uses it directly.
xdg_shell_xml = wayland_protocols_dir / 'stable/xdg-shell/xdg-shell.xml'

xdg_shell_header = custom_target(
  'xdg-shell-client-header',
  input: xdg_shell_xml,
  output: 'xdg-shell-client-protocol.h',
  command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
)

xdg_shell_source = custom_target(
  'xdg-shell-client-source',
  input: xdg_shell_xml,
  output: 'xdg-shell-client-protocol.c',
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
)

# 'namespace' is a C++ keyword; the protocol uses it as an argument name, so
# rename it to 'name_space' in the generated code (same fix noctalia uses).
layer_shell_header = custom_target(
  'wlr-layer-shell-client-header',
  input: 'protocols/wlr-layer-shell-unstable-v1.xml',
  output: 'wlr-layer-shell-unstable-v1-client-protocol.h',
  command: [
    'sh', '-c', '"$1" client-header "$2" "$3" && sed -i s/namespace/name_space/g "$3"',
    '_', wayland_scanner.full_path(), '@INPUT@', '@OUTPUT@'
  ],
)

layer_shell_source = custom_target(
  'wlr-layer-shell-client-source',
  input: 'protocols/wlr-layer-shell-unstable-v1.xml',
  output: 'wlr-layer-shell-unstable-v1-client-protocol.c',
  command: [
    'sh', '-c', '"$1" private-code "$2" "$3" && sed -i s/namespace/name_space/g "$3"',
    '_', wayland_scanner.full_path(), '@INPUT@', '@OUTPUT@'
  ],
)

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
  
  proto_header = custom_target(
    proto_name + '-client-header',
    input: proto_xml,
    output: proto_name + '-client-protocol.h',
    command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
  )
  
  proto_source = custom_target(
    proto_name + '-client-source',
    input: proto_xml,
    output: proto_name + '-client-protocol.c',
    command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
  )
  
  idle_protocol_headers += proto_header
  idle_protocol_sources += proto_source
endforeach

# cursor-shape-v1: lets the compositor draw the pointer image (arrow /
# pointing-hand / etc.) by name, so kokusei never has to load a cursor
# theme or ship cursor bitmaps itself. Shared by both binaries because
# service/pointer.cpp (which references it) is linked into both.
cursor_shape_xml = wayland_protocols_dir / 'staging/cursor-shape/cursor-shape-v1.xml'

cursor_shape_header = custom_target(
  'cursor-shape-v1-client-header',
  input: cursor_shape_xml,
  output: 'cursor-shape-v1-client-protocol.h',
  command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
)

cursor_shape_source = custom_target(
  'cursor-shape-v1-client-source',
  input: cursor_shape_xml,
  output: 'cursor-shape-v1-client-protocol.c',
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
)

# cursor-shape-v1's get_tablet_tool_v2 request references zwp_tablet_tool_v2,
# so tablet-unstable-v2's generated code has to be linked in too even though
# nothing here uses tablets directly (same situation as xdg-shell above).
tablet_v2_xml = wayland_protocols_dir / 'unstable/tablet/tablet-unstable-v2.xml'

tablet_v2_header = custom_target(
  'tablet-v2-client-header',
  input: tablet_v2_xml,
  output: 'tablet-unstable-v2-client-protocol.h',
  command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
)

tablet_v2_source = custom_target(
  'tablet-v2-client-source',
  input: tablet_v2_xml,
  output: 'tablet-unstable-v2-client-protocol.c',
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
)

# hyprland-toplevel-export-v1: per-window live capture for the overview
# module's workspace-grid window thumbnails. Hyprland-specific, mirrors
# wlr-layer-shell's plain (non-idle-protocol) custom_target shape above.
#
# Its v2 request capture_toplevel_with_wlr_toplevel_handle references
# zwlr_foreign_toplevel_handle_v1 (unused here - kokusei only calls v1's
# capture_toplevel by numeric window address), so wlr-foreign-toplevel-
# management-unstable-v1's generated code has to be linked in too, same
# situation as xdg-shell/tablet-v2 above.
toplevel_export_header = custom_target(
  'hyprland-toplevel-export-client-header',
  input: 'protocols/hyprland-toplevel-export-v1.xml',
  output: 'hyprland-toplevel-export-v1-client-protocol.h',
  command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
)

toplevel_export_source = custom_target(
  'hyprland-toplevel-export-client-source',
  input: 'protocols/hyprland-toplevel-export-v1.xml',
  output: 'hyprland-toplevel-export-v1-client-protocol.c',
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
)

foreign_toplevel_xml = 'protocols/wlr-foreign-toplevel-management-unstable-v1.xml'

foreign_toplevel_header = custom_target(
  'foreign-toplevel-client-header',
  input: foreign_toplevel_xml,
  output: 'wlr-foreign-toplevel-management-unstable-v1-client-protocol.h',
  command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
)

foreign_toplevel_source = custom_target(
  'foreign-toplevel-client-source',
  input: foreign_toplevel_xml,
  output: 'wlr-foreign-toplevel-management-unstable-v1-client-protocol.c',
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
)

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
  'src/render/toplevel_window.cpp',
  'src/render/slider.cpp',
  'src/render/matrix_grid.cpp',
  'src/render/video_texture.cpp',
]

# service sources: each dbus service is a merged types+sdbus-c++ pair, and
# keyboard/pointer/active_output/system_telemetry are exercised directly by
# their tests, so both the main executable and the test binary link these
# (test/dbus/*.cpp calls the parsing functions that live in the dbus ones,
# which pulls in sdbus_cpp_dep for the test binary too). pipewire.cpp links
# libpipewire and frame_clock/output_scale/layer_surface/hyprland/shojiwm
# have no test coverage - layer_surface.cpp in particular needs the
# wlr-layer-shell generated protocol code - so those stay main-executable-only.
kokusei_service_test_sources = [
  'src/service/bluetooth_service.cpp',
  'src/service/network_service.cpp',
  'src/service/tray_service.cpp',
  'src/service/upower_service.cpp',
  'src/service/mpris_service.cpp',
  'src/service/system_telemetry.cpp',
  'src/service/keyboard.cpp',
  'src/service/pointer.cpp',
  'src/service/active_output.cpp',
  'src/service/wallpaper_service.cpp',
  'src/service/wallpaper_animate_service.cpp',
  'src/service/settings_service.cpp',
  'src/service/icon_theme.cpp',
]

kokusei_service_main_only_sources = [
  'src/service/pipewire.cpp',
  'src/service/volume_slider.cpp',
  'src/service/frame_clock.cpp',
  'src/service/output_scale.cpp',
  'src/service/layer_surface.cpp',
  'src/service/hyprland.cpp',
  'src/service/shojiwm.cpp',
  'src/service/toplevel_export.cpp',
  'src/service/audio_spectrum.cpp',
  'src/service/wallpaper_hw_decode_loader.cpp',
]

# launcher.cpp now only touches EGL/GL/Wayland for the panel itself (the
# 8 provider sections split out into src/launcher/*, tested directly), so it
# no longer belongs in the shared/test-linked source list.
kokusei_launcher_main_only_sources = [
  'src/modules/launcher.cpp',
]

# src/launcher/*: pure logic, no EGL/Wayland/Renderer, exercised directly by
# test/launcher/test_launcher.cpp, shared between both binaries.
kokusei_launcher_test_sources = [
  'src/launcher/apps_provider.cpp',
  'src/launcher/desktop_entry.cpp',
  'src/launcher/files_provider.cpp',
  'src/launcher/launch_action.cpp',
  'src/launcher/search.cpp',
  'src/launcher/submenu.cpp',
  'src/launcher/visit_store.cpp',
]

# bar/panel/**: real EGL/GL rendering, no test coverage, main-executable-only.
kokusei_bar_panels_main_only_sources = [
  'src/bar/panel/tray_panel.cpp',
  'src/bar/panel/network_panel.cpp',
  'src/bar/panel/bluetooth_panel.cpp',
  'src/bar/panel/volume_panel.cpp',
  'src/bar/panel/battery_panel.cpp',
  'src/bar/panel/system_monitor_panel.cpp',
]

# bar/ itself + widget/: real EGL/GL rendering, no test coverage,
# main-executable-only.
kokusei_bar_main_only_sources = [
  'src/modules/bar.cpp',
  'src/bar/widget/widget_capsule.cpp',
  'src/bar/widget/clock_widget.cpp',
  'src/bar/widget/workspace_widget.cpp',
  'src/bar/widget/battery_widget.cpp',
  'src/bar/widget/starward_widget.cpp',
  'src/bar/widget/bluetooth_widget.cpp',
  'src/bar/widget/network_widget.cpp',
  'src/bar/widget/dashboard_widget.cpp',
  'src/bar/widget/volume_widget.cpp',
]

kokusei_idle_main_only_sources = [
  'src/modules/idle.cpp',
]

# osd/, wallpaper/, notification/: real EGL/GL rendering, no test coverage,
# main-executable-only.
kokusei_overlay_main_only_sources = [
  'src/modules/osd.cpp',
  'src/modules/wallpaper.cpp',
  'src/modules/notification.cpp',
  'src/modules/starward.cpp',
  'src/modules/dashboard.cpp',
  'src/modules/overview.cpp',
  'src/modules/matrix.cpp',
  'src/modules/visualizer.cpp',
]

# visualizer/*: real EGL/GL rendering, no test coverage, main-executable-only.
kokusei_visualizer_main_only_sources = [
  'src/visualizer/sphere_visualizer.cpp',
  'src/visualizer/bar_visualizer.cpp',
]

# app/config.cpp is exercised by test/app/test_config.cpp and
# test/app/test_wallpaper_resolve.cpp; single_instance_lock.cpp has no test.
kokusei_app_test_sources = [
  'src/app/config.cpp',
]

kokusei_app_main_only_sources = [
  'src/app/single_instance_lock.cpp',
  'src/app/ipc.cpp',
  'src/app/key_dispatch.cpp',
  'src/app/module_registry.cpp',
  'src/app/monitor_output.cpp',
  'src/app/service_registry.cpp',
  'src/app/user_info.cpp',
  'src/app/wayland_registry.cpp',
]

# settings/: real EGL/GL rendering, no test coverage, main-executable-only.
kokusei_settings_main_only_sources = [
  'src/modules/settings.cpp',
  'src/settings/wallpaper_tab.cpp',
  'src/settings/displays_tab.cpp',
  'src/settings/idle_tab.cpp',
  'src/settings/visualizer_tab.cpp',
]

kokusei_split_sources = [
  kokusei_core_split_sources,
  kokusei_render_test_sources,
  kokusei_render_main_only_sources,
  kokusei_service_test_sources,
  kokusei_service_main_only_sources,
  kokusei_launcher_test_sources,
  kokusei_launcher_main_only_sources,
  kokusei_idle_main_only_sources,
  kokusei_overlay_main_only_sources,
  kokusei_app_test_sources,
  kokusei_app_main_only_sources,
  kokusei_settings_main_only_sources,
  kokusei_bar_panels_main_only_sources,
  kokusei_bar_main_only_sources,
  kokusei_visualizer_main_only_sources,
]

kokusei_test_split_sources = [
  kokusei_core_split_sources,
  kokusei_render_test_sources,
  kokusei_service_test_sources,
  kokusei_launcher_test_sources,
  kokusei_app_test_sources,
]

# Animated-wallpaper decode is the only thing in kokusei that touches
# ffmpeg, and ffmpeg's SONAME isn't stable across releases (see readme.md).
# Building it as a dlopen'd plugin instead of linking it into the main
# binary means a missing/mismatched ffmpeg only disables animated
# wallpapers (service/wallpaper_hw_decode_loader.cpp falls back
# gracefully) instead of preventing the whole shell from starting.
kokusei_wallpaper_hw_decode_plugin_dir = kokusei_data_dir / 'plugins'

wallpaper_hw_decode_plugin = shared_module(
  'kokusei-wallpaper-hw-decode',
  'src/service/wallpaper_hw_decode.cpp',
  # klog() isn't exported by the main kokusei binary (no -rdynamic), so a
  # dlopen'd plugin can't resolve it against the host - the plugin needs
  # its own copy. log.cpp is a stateless append-only file logger, so two
  # independent instances (host + plugin) writing the same log file is fine.
  'src/core/log.cpp',
  include_directories: kokusei_inc,
  dependencies: [
    libavformat_dep,
    libavcodec_dep,
    libavfilter_dep,
    libavutil_dep,
    libdrm_dep,
  ],
  install: true,
  install_dir: kokusei_wallpaper_hw_decode_plugin_dir,
)

executable(
  'kokusei',
  'src/kokusei.cpp',
  kokusei_split_sources,
  layer_shell_header,
  layer_shell_source,
  xdg_shell_header,
  xdg_shell_source,
  idle_protocol_headers,
  idle_protocol_sources,
  cursor_shape_header,
  cursor_shape_source,
  tablet_v2_header,
  tablet_v2_source,
  toplevel_export_header,
  toplevel_export_source,
  foreign_toplevel_header,
  foreign_toplevel_source,
  include_directories: kokusei_inc,
  dependencies: [
    wayland_client_dep,
    wayland_egl_dep,
    egl_dep,
    glesv2_dep,
    tomlplusplus_dep,
    cairo_dep,
    pangocairo_dep,
    nlohmann_json_dep,
    sdbus_cpp_dep,
    freetype2_dep,
    libpipewire_dep,
    xkbcommon_dep,
    threads_dep,
    libpng_dep,
    libjpeg_dep,
    libdrm_dep,
  ],
  cpp_args: [
    '-DKOKUSEI_FONT_DIR="' + kokusei_font_dir + '"',
    '-DKOKUSEI_DEFAULT_WALLPAPER="' + kokusei_data_dir / 'default.png' + '"',
    '-DKOKUSEI_BULLET_DIR="' + kokusei_bullet_dir + '"',
    '-DKOKUSEI_YUJIMAI_FONT="' + kokusei_font_dir / 'YujiMai.ttf' + '"',
    '-DKOKUSEI_STARWARD_LOGO="' + kokusei_data_dir / 'logo.png' + '"',
    '-DKOKUSEI_WALLPAPER_HW_DECODE_PLUGIN="' +
        kokusei_wallpaper_hw_decode_plugin_dir /
        'libkokusei-wallpaper-hw-decode.so' + '"',
  ],
  install: true,
  install_dir: 'bin',
)

kokusei_test = executable(
  'kokusei-test',
  'test/kokusei-test.cpp',
  kokusei_test_split_sources,
  layer_shell_header,
  layer_shell_source,
  xdg_shell_header,
  xdg_shell_source,
  cursor_shape_header,
  cursor_shape_source,
  tablet_v2_header,
  tablet_v2_source,
  'test/app/test_config.cpp',
  'test/app/test_wallpaper_resolve.cpp',
  'test/core/test_async_process.cpp',
  'test/core/test_deferred_call.cpp',
  'test/core/test_poll_source.cpp',
  'test/dbus/test_network_parse.cpp',
  'test/dbus/test_bluetooth.cpp',
  'test/dbus/test_mpris.cpp',
  'test/launcher/test_launcher.cpp',
  'test/wayland/test_keyboard.cpp',
  'test/wayland/test_active_output.cpp',
  'test/system/test_rfkill.cpp',
  'test/system/test_cpu_temp.cpp',
  'test/system/test_gpu_temp.cpp',
  'test/system/test_system_stats.cpp',
  'test/render/test_animation.cpp',
  'test/render/test_palette.cpp',
  'test/render/test_image_decode.cpp',
  include_directories: kokusei_inc,
  dependencies: [
    threads_dep,
    tomlplusplus_dep,
    xkbcommon_dep,
    wayland_client_dep,
    wayland_egl_dep,
    egl_dep,
    cairo_dep,
    pangocairo_dep,
    freetype2_dep,
    sdbus_cpp_dep,
    libpng_dep,
    libjpeg_dep,
    glesv2_dep,
  ],
  cpp_args: [
    '-DKOKUSEI_DEFAULT_WALLPAPER="' + kokusei_data_dir / 'default.png' + '"',
    '-DKOKUSEI_BULLET_DIR="' + kokusei_bullet_dir + '"',
    '-DKOKUSEI_FONT_DIR="' + kokusei_font_dir + '"',
  ],
)

test('kokusei', kokusei_test)