# Idle port — execution checklist

Source plan: `local/plan/idle-port.md`. Real idle module lives at
`src/modules/idle.{h,cpp}` (plan said `src/service/idle.*` — stale path,
used the real one).

- [x] Phase 1 — Config & resolvers (`src/app/config.h/.cpp`)
- [x] Phase 2 — Per-monitor idle clock (`src/modules/idle.h/.cpp`,
      `src/app/service_registry.cpp`)
- [x] Phase 3 — Screensaver + ambient overlay module
      (`src/modules/idle_overlay.{h,cpp}` NEW, `src/modules/wallpaper.h/.cpp`
      extraction, `src/render/renderer.h/.cpp` + shader video-opacity)
- [x] Phase 4 — Wallpaper pause rewire (`src/app/module_registry.h/.cpp`)
- [x] Phase 5 — Settings idle tab rewrite (`src/settings/idle_tab.h/.cpp`,
      `src/settings/displays_tab.h/.cpp` selector-row export,
      `src/modules/settings.h/.cpp`, `src/config/settings_config.h`,
      `src/service/settings_service.h/.cpp`)
- [x] meson.build — register `src/modules/idle_overlay.cpp`
- [x] Build verification (`meson compile` clean, `meson test` passing)

## Deviations from the written plan (all judgment calls, not asked-about)

- Idle module is at `src/modules/idle.*`, not `src/service/idle.*` as the
  plan text said — used the real path.
- Added `Config::idle_management_enabled` (global "Enable Idle Management"
  toggle). The plan's phase-1 field list omitted it, but the Settings
  section and tab layout both require it, so it was added and folded into
  `ambient_effective_enabled`/`screensaver_effective_enabled` as a master
  gate.
- Reset-to-default icon on the Ambient/Screensaver tiles is always visible
  (when the value differs from the global default) rather than
  hover-gated — `SettingsState` has no per-pixel hover tracking today and
  adding it was out of proportion to this task.
- Screensaver logo: loads its own copy of `assets/logo.png` at
  `idle_overlay_init_egl` time instead of borrowing
  `StarwardModule::logo_tex` — the first monitor's per-monitor modules
  finish `init_egl` *before* `app.overlays` (where Starward lives) is even
  constructed, so the shared-texture pointer would be null on launch for
  monitor 1. Loading independently sidesteps the ordering hazard.
- Extended `Renderer::draw_video_texture_rect`/the video fragment shader
  with a `u_opacity` uniform so the ambient overlay can fade a zero-copy
  (VAAPI) animated wallpaper column, not just the CPU-decoded path.
