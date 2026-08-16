## src/app

- config.h / config.cpp
- single_instance_lock.h / single_instance_lock.cpp
- ipc.h / ipc.cpp
- key_dispatch.h / key_dispatch.cpp
- monitor_output.h / monitor_output.cpp
- module.h (`Module` interface, per-surface overlay boundary)
- per_monitor_module.h (`PerMonitorModule` interface, per-surface per-monitor boundary)
- module_registry.h / module_registry.cpp (`build_app_modules`/`build_per_monitor_modules` composition root)
- wayland_registry.h / wayland_registry.cpp
- wayland_state.h (`WaylandState`, composition root struct)
- service.h (`Service` interface, process-wide poll/tick boundary)
- service_registry.h / service_registry.cpp (`build_services` composition root)

## src/config

- bar_config.h
- launcher_config.h
- osd_config.h
- notification_config.h
- starward_config.h
- controlcenter_config.h (geometry constants mirror keqing-shell's `ControlCenterConfig.qml`, one group per card)
- wallpaper_config.h
- settings_config.h
- matrix_config.h
- visualizer_config.h

## src/render

- palette.h
- color_ops.h
- texture.h / texture.cpp
- panel_scroll.h / panel_scroll.cpp
- text.h / text.cpp
- renderer.h / renderer.cpp
- rect.h
- panel_chrome.h / panel_chrome.cpp
- node.h / node.cpp
- gl.h / gl.cpp
- overlay_panel.h / overlay_panel.cpp
- toplevel_window.h / toplevel_window.cpp
- scene.h
- image.h / image.cpp
- texture_cache.h / texture_cache.cpp
- icon.h / icon.cpp
- icons.h
- text_field.h / text_field.cpp
- animation.h / animation.cpp
- slider.h / slider.cpp
- matrix_grid.h / matrix_grid.cpp

## src/service

- bluetooth_service.h / bluetooth_service.cpp
- network_service.h / network_service.cpp
- tray_service.h / tray_service.cpp
- mpris_service.h / mpris_service.cpp
- upower_service.h / upower_service.cpp
- pipewire.h / pipewire.cpp
- volume_slider.h / volume_slider.cpp
- system_telemetry.h / system_telemetry.cpp
- frame_clock.h / frame_clock.cpp
- output_scale.h / output_scale.cpp
- layer_surface.h / layer_surface.cpp
- keyboard.h / keyboard.cpp
- pointer.h / pointer.cpp
- hyprland.h / hyprland.cpp
- shojiwm.h / shojiwm.cpp
- active_output.h / active_output.cpp
- workspace.h
- wallpaper_service.h / wallpaper_service.cpp
- settings_service.h / settings_service.cpp
- icon_theme.h / icon_theme.cpp
- audio_spectrum.h / audio_spectrum.cpp

## src/core

- deferred_call.h / deferred_call.cpp
- log.h / log.cpp
- sdbus_poll_source.h
- poll_source.h / poll_source.cpp
- async_process.h / async_process.cpp

## src/modules

- bar.h / bar.cpp
- launcher.h / launcher.cpp (surface/EGL/tick/toggle/key/click/paint core; provider logic in src/launcher)
- osd.h / osd.cpp
- notification.h / notification.cpp
- starward.h / starward.cpp
- controlcenter.h / controlcenter.cpp (fixed-order card layout inlined, no per-card files)
- wallpaper.h / wallpaper.cpp
- idle.h / idle.cpp
- settings.h / settings.cpp
- matrix.h / matrix.cpp
- visualizer.h / visualizer.cpp

## src/launcher

- apps_provider.h / apps_provider.cpp
- desktop_entry.h / desktop_entry.cpp
- files_provider.h / files_provider.cpp
- launch_action.h / launch_action.cpp
- search.h / search.cpp
- submenu.h / submenu.cpp
- visit_store.h / visit_store.cpp

## src/settings

- wallpaper_tab.h / wallpaper_tab.cpp
- displays_tab.h / displays_tab.cpp
- idle_tab.h / idle_tab.cpp

## src/bar

- panel/network_panel.h / panel/network_panel.cpp
- panel/bluetooth_panel.h / panel/bluetooth_panel.cpp
- panel/volume_panel.h / panel/volume_panel.cpp
- panel/tray_panel.h / panel/tray_panel.cpp
- widget/widget_capsule.h / widget/widget_capsule.cpp
- widget/workspace_widget.h / widget/workspace_widget.cpp
- widget/clock_widget.h / widget/clock_widget.cpp
- widget/starward_widget.h / widget/starward_widget.cpp
- widget/battery_widget.h / widget/battery_widget.cpp
- widget/network_widget.h / widget/network_widget.cpp
- widget/bluetooth_widget.h / widget/bluetooth_widget.cpp
- widget/volume_widget.h / widget/volume_widget.cpp
- widget/control_center_widget.h / widget/control_center_widget.cpp

## src

- kokusei.cpp

## test

- kokusei-test.cpp
- kokusei-test.hpp
- app/test_config.cpp
- app/test_wallpaper_resolve.cpp
- core/test_async_process.cpp
- core/test_deferred_call.cpp
- core/test_poll_source.cpp
- dbus/test_network_parse.cpp
- dbus/test_bluetooth.cpp
- launcher/test_launcher.cpp
- wayland/test_keyboard.cpp
- system/test_rfkill.cpp
- render/test_animation.cpp
- render/test_palette.cpp
- render/test_image_decode.cpp
- dbus/test_mpris.cpp
- system/test_cpu_temp.cpp
- system/test_gpu_temp.cpp
- system/test_system_stats.cpp
