# `kokusei` development critical knowledge

## Description

Hard-won rules from kokusei's development.
Can be updated if found new knowledge that supersedes old ones, or genuinely new ones

## Rule

One statement + One explanation, ≤ 20 words each.

## 1. Subprocesses in a multithreaded process

- **The poll loop must never block, even briefly.** One poll() loop drives every surface; a single blocking call freezes the whole shell.
- **kokusei is multithreaded even though it wrote no threads itself.** Mesa, pipewire, and Pango each spawn their own background threads automatically.
- **Never allocate memory in a forked child before exec().** fork() can copy a lock held by another thread, deadlocking the child forever.
- **Ignoring SIGCHLD and calling waitpid() cannot coexist.** Ignoring SIGCHLD is process-wide and lets the kernel auto-reap, breaking waitpid() everywhere.
- **Fire-and-forget processes should double-fork, not rely on global reaping.** An intermediate child exits immediately, reparenting the grandchild to init for automatic reaping.
- **A dedicated blocking-waitpid thread beats watching a child's fd in the poll loop.** waitpid() guarantees instant return on exit; poll() sometimes missed pipe readiness for tens of seconds.
- **A cancelled background process must be killed, not just detached.** Leaving it running lets retyped searches pile up competing processes that never finish.
- **SIGKILL stops future CPU use but doesn't guarantee immediate process death.** Confirm actual death before forking a replacement, or the two processes compete.
- **Reusing a handle across restarts needs a generation counter.** A cancelled worker can still wake later with a stale result unless generations are checked.

## 2. Debugging methodology

- **Three plausible root-cause theories were wrong before the real one was found.** Every theory was ruled out by measurement, not argument.
- **A /proc-based process watcher plus timestamped logs and gdb backtraces located the real bug.** Only correlating both sides' timestamps distinguished "child is slow" from "we missed it finishing."
- **A stopgap workaround left after its cause is fixed becomes a silent regression.** `fd --threads 4` stayed after the real fix, silently halving all search parallelism.
- **Fix the shared function, not just the caller that reported the bug.** The same defect usually routes through every sibling caller too.
- **Restore `kernel.yama.ptrace_scope` to 1 after live debugging.** It was lowered to 0 temporarily so gdb could attach without sudo.

## 3. Rendering

- **Don't render the Tabler icon font via fontconfig plus Pango.** Late-registered app fonts aren't reliably picked up by Pango's font map; use FreeType+Cairo directly.
- **kokusei clips using scissor rects plus a corner inset, not a stencil buffer.** This is correct as long as nothing needs to visually touch a rounded edge.
- **A scissor clip helper needs a stack, not one slot, once clips can nest.** An unconditional glDisable on destruction wiped the outer clip when clips nested.
- **A container animating its own size must clip children to the current size.** Fading opacity alone doesn't stop oversized content rendering outside the container mid-tween.
- **A cached geometry value must derive from the same variable used for drawing.** Using the animation's target width instead of the current frame produced a wrong stored position.
- **Scissor rects should floor/ceil each edge independently, not truncate uniformly.** Flooring the origin then rounding the size can drop the last row or column.
- **A narrower anti-aliasing band makes rounded-rect edges look crisper.** kokusei's 2px smoothstep band softened straight edges; matching noctalia's 1px band fixed it.
- **Drawing textures at fractional pixel positions blurs every glyph and icon.** GL_LINEAR sampling blends edge texels 50/50 at .5px offsets; round positions before drawing.
- **`show_layout`'s current point is the top-left corner, not the baseline.** Adding ascent on top of that doubled the offset, rendering text clipped near the bottom.
- **`set_opacity()` is a single global value per frame, not per-node.** Simultaneous different opacities need baking alpha into each element's own color instead.
- **A bounded `stat()` search beats a subprocess search over a handful of candidates.** Icon resolution only needs existence checks against known paths, not an open-ended directory search.
- **Writing literal Unicode escapes through tool calls is unsafe.** The JSON layer can silently replace them with actual glyph bytes; verify with `od -c`.
- **Verify icon names against the widget's actual default-state property, not a plausible name.** Several icons were wrong because they were guessed from a config property list.
- **Size text/icon textures from fixed font metrics, not per-string ink extents.** Ink-based sizing made baseline position jitter as string content changed between renders.
- **Cairo output is premultiplied alpha, but kokusei's blend convention is straight alpha.** Uploading one as the other silently squares alpha at edges, washing out antialiased pixels.
- **`draw_rounded_rect` always reads its border-color argument, even at zero border width.** Passing nullptr for "no border" is a null-pointer read, not a no-op.
- **A rebuilt-every-frame node tree should pool and reuse nodes, not reallocate.** Reallocating at animation frame rate causes unbounded heap high-water-mark growth over time.
- **A refactor changing a shared function's contract must migrate every call site.** Leaving old `add_child()` around let stragglers silently skip rendering after the pooling refactor.
- **An animated per-frame layout value needs a snap-on-first-value sentinel before tweening.** Without one, the first frame animates from a stale or zero default.
- **A tweened value must not be computed from another value that's itself mid-tween.** Otherwise it re-targets every frame instead of converging on a moving anchor.
- **AnimationManager has no delay primitive, so chain a no-op tween to get one.** `animate()`/`animateTimer()` both start immediately; a dummy tween's `on_complete` triggers the real one.
- **A module with its own entrance animation should skip the generic overlay-panel fade.** Layering both fades produces a visible double-fade effect the reference lacks.
- **A pre-upload downsample must derive its target size from the source's aspect ratio.** Squashing to the destination box's raw dimensions bakes in a stretch a later crop can't undo.
- **A per-element opacity tween must not run while a container-level fade is active.** Two independently-timed alpha ramps multiply into a visibly different, non-obvious result.
- **Node color pointers are read at draw time, not when stored, so must outlive the frame.** Passing a temporary Color's address renders garbage once the stack slot is reused.
- **`node_add_texture` draws at native pixel size, not scaled to its container.** Placing an aspect-preserving decode in a fixed cell without cropping spills into neighbors.
- **The rasterize-once-cache-by-key pattern generalizes beyond text/icons to any procedural drawing.** Cache keys must bucket continuous inputs like percentages, or entries grow unbounded.
- **A custom multi-pass GPU shader effect bypasses the Node/Scene graph entirely.** `Node`/`Renderer` only build rect/rrect/texture draws; give the effect its own programs/FBOs and call it directly from the module's paint function alongside `scene.draw()`.
- **GLSL ES 1.00's `for` loop bound must be a compile-time constant, not a uniform.** A tunable `#define` controlling a loop trip count (e.g. sample count) has to stay a literal `const` in the shader source, not a runtime-settable uniform.
- **A per-frame multi-tap fullscreen shader pass at native output resolution can stall the shared single poll loop.** `ncs_visualizer.cpp`'s 96-tap glow blur froze the whole shell until it ran on a downsampled FBO instead.
- **A per-frame `glGenBuffers`/`glDeleteBuffers` for a static quad is driver churn every other buffer in the codebase avoids.** Create once alongside the program/VBO it belongs to, like `Renderer::quad_vbo_` and `particle_vbo`.
- **Resolution/tap-count tuning alone couldn't stop `ncs_visualizer` from stalling the shared poll loop, since it still ran synchronously inside it.** `local/request.md` authorized a scoped exception to the single-context rule (#115): the visualizer window now runs on its own thread with its own share-context `EGLContext`, owning its `EGLSurface` exclusively while open; every other surface stays on the single shared context.
- **A dedicated render thread that shares an `EGLSurface` with the main thread's context needs the two to never be current on it at once.** The visualizer's render thread owns `state.base.egl_surface` outright for the whole time the window is open; `visualizer_paint` on the main thread only hands off a small per-frame struct (shape, spectrum, opacity, time, dimensions) under a mutex and never touches GL for this window at all.
- **Two hand-rolled opacity pipelines for two shapes of the same window drift apart silently.** kokusei's per-shape special case gave `bars` and `ncs` independent opacity handling; `ncs`'s composite pass ended up clearing its background at full alpha regardless of the window's fade-in, while `bars`' background faded correctly through `Renderer::set_opacity`. Following `noctalia`'s model (one dedicated render thread, both shapes as ordinary draws sharing one `Scene`/`Renderer` opacity path) removed the second pipeline instead of patching it to match the first.
- **A single shared `Renderer` needs its own reset at frame start, not caller discipline.** `bar.cpp` and `settings.cpp` set `opacity_` without restoring it to `1.0f`, leaking a stale fade value into `wallpaper.cpp`'s next paint on the same shared `Renderer` (clearing or flashing the wallpaper); `Renderer::begin_frame()` now resets `opacity_` to `1.0f` itself, fixing every caller including future ones instead of patching each offender.
- **A freshly created share-context starts with `GL_BLEND` disabled, even sharing a namespace with a context that has it enabled.** `ncs_visualizer_render`'s passes set `glBlendFunc` but never `glEnable(GL_BLEND)` on the visualizer render thread's context, since only the `bars` path routed through `Renderer::init()`; a raw GL-passes-only path must enable it itself once per context lifetime.
- **`glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)` squares alpha when a translucent rect draws over a transparent-cleared surface.** The same factor applies to the alpha channel as color, computing `src.a * src.a` instead of `src.a`; `Renderer::begin_frame()` uses `glBlendFuncSeparate(..., GL_ONE, GL_ONE_MINUS_SRC_ALPHA)` for the alpha channel so a rect's alpha lands exactly where its caller set it.
- **A reference shader's final coordinate remap is easy to drop when porting the math piecewise.** `ncs/1.frag`'s last line squeezes every particle's position by `(coords + center) / 2` before writing it; `ncs_visualizer.cpp`'s `kParticleVs` ported the noise/sphere-pull math but wrote straight to NDC, so the sphere rendered oversized and off-window (`local/plan/sphere-visualizer-missing-coordinate-compression.md`).

## 4. Wayland protocol

- **Wayland gives no way to query which output the pointer is over, live.** Track it as a best-effort hint from your own surfaces' enter/motion events instead.
- **Optional protocol events need sane fallback defaults, not zero.** Some compositors never send `repeat_info`; defaulting to 0/0 silently disables key repeat.
- **Never live-test input-grabbing or lock-screen protocol code carelessly.** A prior live test hung the keyboard and forced a reboot.
- **A coordinate from one layer-shell surface isn't valid on another without translating margins.** Different surfaces can have different margins, so origins don't automatically align.
- **A click-through popup needs a small anchored surface, not a full-output one.** A full-output surface would swallow every pointer event across the whole screen while open.
- **A fading, resizing surface should tween opacity only and snap geometry at the endpoints.** Driving both from one tween made content visibly rescale instead of cleanly fading.
- **Hover-driven surface changes must only touch size, never margin or exclusive_zone.** Changing margin on hover repositioned the surface mid-hover, causing an infinite flicker loop.
- **`exclusive_zone` must not include the same-edge margin value.** The compositor already adds that margin automatically, so including it reserves extra space twice.
- **An output's initial event burst isn't guaranteed by the same roundtrip that bound it.** A bind's reply is a second round-trip; do one extra roundtrip before reading output state.
- **A live-update IPC event may lack a field only a full snapshot query provides.** Check whether an adjacent event in the same stream already carries and can cache that field.
- **Don't start the polkit agent before keyboard input and a GLib main loop both exist.** An agent that registers but can't prompt intercepts and fails every real `pkexec` system-wide.
- **A struct member can't share a name with a Wayland protocol type used in the same header.** `xdg_surface *xdg_surface` compiles with `-Wchanges-meaning` and later breaks name lookup in any including translation unit; name the field differently from its pointee type.
- **A real `xdg_toplevel` window is created on open and destroyed on close, not kept mapped-but-transparent.** Unlike a layer-shell overlay's fade-in-place trick, a zero-opacity mapped toplevel would still show in window switchers/taskbars.
- **`ToplevelWindowBase` has no generic resize callback, only `on_close_request`.** A module keeping a persistent per-size buffer (like `MatrixGrid`'s raster) must track the size it last built for itself and compare against live `base.width`/`height` each paint, or a live resize stretches the stale buffer instead of re-simulating at the new size.
- **An animation's `on_complete` that destroys the surface it's animating can fire mid-frame, inside the paint function's own `tick()` call.** Re-check the surface handle for validity immediately after `tick()`, before touching any EGL call that assumed it still exists. Where the paint function isn't already re-checked this way (`overlay_panel_toggle`, `launcher_toggle`, `starward.cpp`'s `finish_close`), the destroy itself is deferred via `DeferredCall::call_later` instead, guarded on the surface still being closed when it drains, so it runs on the next poll iteration rather than inside the same `tick()` call.
- **A layer-shell overlay window now destroys its surface on close and lazily recreates it on the next open, instead of keeping it mapped for the process's lifetime.** `overlay_panel_toggle` (`src/render/overlay_panel.cpp`) does this for every `OverlayPanelBase` consumer that routes through it (network/bluetooth/volume/tray panels, tray menu, control center, settings); `starward.cpp`'s `finish_close` and `launcher.cpp`'s close path do the same by hand since they don't call the shared function. Recreation on same-output reopen is done by widening each module's existing "retarget if the target output changed" check to also fire when `base.layer_surface`/`state_.layer_surface` is null (see `module_registry.cpp`, `starward_ipc_handlers`, `controlcenter_ipc_handlers`, `settings_ipc_handlers`); the bar's own popovers instead call the new `overlay_panel_ensure()` template helper (`src/render/overlay_panel.h`) directly at their widget click handlers, since they don't have a retarget path to reuse. A closed window/panel now costs no GPU buffers instead of holding a full-size `wl_egl_window` and swapchain forever.
- **Tearing down a `ToplevelWindowBase` must also reset its `FrameClock` fields, not just the Wayland/EGL handles.** A stale non-null `frame_clock.callback` makes `request_frame` silently no-op forever on the next open, since the surface that owned that pending callback is already gone and will never send `done`.
- **A pending `wl_callback` must be released with `wl_callback_destroy`, never just null the pointer.** `wl_surface_destroy` does not implicitly destroy a separate `wl_callback` object obtained from `wl_surface_frame`; nulling the field without destroying it leaves the object registered, and if the compositor still delivers its `done` event later, the listener fires against a reused, already-reset struct and corrupts the next session's frame bookkeeping.
- **Matrix and visualizer are meant to tile as regular windows, not float.** Do not add a `float = true` window rule for `kokusei-matrix`/`kokusei-visualizer` in `~/keqing-dots/.config/hypr/hyprland.lua`, that is a deliberate rejected direction, not an oversight.
- **A destroy-on-close `xdg_toplevel` can hand its next `xdg_surface`/window handle the exact address a prior instance had.** Any compositor-side or Lua-side per-window state keyed by address/handle (e.g. `~/keqing-dots/.config/hypr/utils/layout.lua`'s `monocle` table) must be cleared when the window goes away, not just on explicit user action, or a reopened `kokusei` window silently inherits a dead window's tracked state (wrong tiling size, tag/border desync) because the compositor reused the old handle.
- **In `hl`'s Lua event API, a window's fields are only safe to read on `window.close`, not `window.destroy`.** By `window.destroy` the underlying window is already torn down, `win.address` (and presumably other fields) reads back `nil`; assigning `nil` to a `nil` table key still raises `table index is nil` in Lua, so an unguarded destroy handler crashes on every window close, not just the one you're targeting. Do cleanup on `window.close` instead.

## 5. Async state correctness

- **Never score an async operation's result against a live mutable field.** Freeze the input into its own field at start time and score against that instead.
- **Capture a value synchronously at the action site rather than deferring to the next repaint.** Relying on an async dispatch path left panels opening at position zero on first use.
- **Click coordinates must be captured atomically with the click event itself.** Reading the live shared pointer position later can return a different monitor's coordinates.
- **A value read right after triggering a state change can still be one frame stale.** Force the downstream paint/tick to run before reading its side effect.
- **Two `animate()` calls sharing an owner id cancel each other, even if unrelated.** Give each simultaneously-animated property of an item its own distinct owner id.
- **AnimationManager only advances when something calls `tick()` every frame.** A panel that forgets to tick freezes forever, including its keyboard-release `on_complete` callback.
- **A reactive `*_changed` flag must be set at every code path that changes the value.** One mutator forgetting the flag silently breaks reactivity for just that path.
- **Binding a PipeWire node listener alone doesn't deliver live param-value updates.** An explicit `pw_node_subscribe_params()` call is required to receive future value changes.
- **Every panel requesting exclusive keyboard interactivity needs its own key-dispatch arm.** The two are declared separately, so nothing enforces they stay in sync as panels are added.
- **An optimistic local write can suppress the `*_changed` flag it's supposed to trigger.** The confirmation compares against the already-updated value and finds no change; raise the flag at the write.
- **A client's own callback confirming a write isn't proof the real state changed.** Device-backed PipeWire nodes need writes routed through the parent Device's Route, not the node.
- **A registry's initial announcement and an object's own info event carry different properties.** A property missing from one may only appear in the other's later event.
- **A generic "click missed" guard excluding a sibling surface pushes the decision onto it.** That surface's own handler must then know about every overlay stacked above it.
- **Calling a shared AnimationManager's `tick()` multiple times per instant is safe if absolute-time-based.** It recomputes from wall-clock time, not accumulated delta, so repeats don't double-advance.
- **A hover-driven highlight must clear on lost surface focus, not just recompute on motion.** Another surface stealing pointer focus mid-hover leaves a stale hovered index unless the dirty-tick handler explicitly clears it when focus moves away.
- **A mutex must cover the read side of a shared buffer, not just the write side.** Splitting `AudioSpectrum::processFrame` into per-channel calls moved its ring-buffer reads outside `ring_mutex_` while `onProcess` (the PipeWire thread) still wrote under it; the reads must stay inside the same locked scope as the writes they race against.

## 6. Architecture and scale discipline

- **noctalia is a reference for ideas, not a template to copy wholesale.** It's roughly 45x kokusei's size; every adopted idea must be resized to kokusei's scale.
- **Several noctalia subsystems were deliberately skipped, not overlooked.** A retained scene graph, backend abstraction, and scripting engine solve problems kokusei doesn't have.
- **Config hot-reload was built, but schema-validated multi-file config remains rejected.** Only when the single file is read changed, not its structure.
- **A config field lacking settings UI may need deletion, not a new control.** Check the reference project first; it may hardcode the same value with no UI either.
- **noctalia's testing philosophy and logic/UI separation already matched kokusei's existing convention.** Naming what was already right is as important as naming what needs to change.
- **Check a reference technique against the full target hardware range, not one profiling machine.** An integrated-GPU-only measurement wrongly justified diverging from a technique discrete GPUs need.
- **A reference's per-output design choice can be a consequence of threading, not correctness.** kokusei is single-threaded, so one shared EGLContext suffices where noctalia needs one per output. **Exception:** the visualizer window (`local/request.md`) gets its own dedicated render thread and share-context `EGLContext`, covering both its `bars` and `ncs` shapes, a scoped, explicitly-authorized deviation for one window whose `ncs` shape's per-frame GPU cost couldn't otherwise be tuned below what stalls the shared poll loop; not a reversal of the general rule, and not extended to any other surface.
- **`noctalia`'s render architecture is one dedicated thread for all GL/scene work, with every visual style as an ordinary `Node` sharing one opacity pipeline, not a thread per special-cased effect.** kokusei's earlier `ncs`-only thread special-case let `bars` and `ncs` diverge into two independently hand-rolled opacity pipelines. Giving the whole visualizer window one thread, with both shapes as draws inside that thread's single frame, matches `noctalia`'s actual model instead of a scaled-down approximation of it.
- **An abstraction earns its place only by removing duplication that exists today.** A planned compositor-backend interface was dropped once its motivating duplication was already merged away.
- **Build a generic primitive only once a second real caller is visible, not imaginable.** `DeferredCall` ended up with no caller and stays documented rather than wired in.
- **A default-plus-override config value must be cached on the consumer's own per-monitor state.** Re-resolving the tier chain on every hot-path read would turn 15 reads into map lookups.
- **A resolved-with-fallback accessor and a raw-override accessor answer different questions and must not be interchanged.** UI that decides whether to show a "remove/clear this override" control needs the raw override only; using the fallback-inclusive resolver there makes the control appear (and no-op) whenever only the fallback is showing.
- **Porting a singleton overlay to per-monitor rendering should split process-wide from per-monitor state.** A D-Bus connection is one-per-process; the render surface is one-per-monitor — don't duplicate both.
- **A cross-cutting service with no surface of its own needs a `Service`, not a `Module`.** `Module`/`PerMonitorModule` own a surface; network/bluetooth/pipewire/tray/mpris/upower/idle/compositor-workspace don't, so `kokusei.cpp` hand-wired their init/tick/poll-fd plumbing until `src/app/service.h`'s `init`/`timer_tick`/`poll_sources` gave them the same generic-loop treatment overlays already had via `extra_poll_sources()`.

## 7. Build and workflow

- **Only the user runs `dist/install` and `dist/run`.** Both perform real sudo actions or launch a live session; `dist/test` is safe to run freely.
- **Batch edits and build once, not after every small change.** Reformat with clang-format after each edit, keeping comments short enough not to wrap.
- **Bare `clang-format -i` silently reformats the whole file to 2-space LLVM default, not the project's 4-space style.** There is no `.clang-format` file in the repo, only `local/important/convention.md`'s documented command, `clang-format -i --style="{IndentWidth: 4}" <file>`. Omitting `--style` inflates the diff with unrelated whitespace churn across the entire file instead of just the touched lines.
- **Tests are plain `main()` plus `<cassert>`, with no framework.** Run timing-sensitive tests repeatedly before trusting them, since races can pass once and fail later.
- **Merging a module's pure logic and EGL/GL code into one file forces graphics deps onto the test binary.** Keep the `*_test_sources`/`*_main_only_sources` split so the test binary stays free of EGL/GL.
- **`meson test` names are the registered test names, not executable file names.** Use `async_process`, not `test_async_process`, when invoking a specific test.
- **Every bundled asset needs the installed-path-plus-dev-tree-fallback loading pattern.** A bare relative path resolves against the daemon's cwd, silently failing outside the source tree.
- **A connect()-to-socket liveness probe is unreliable against a leftover socket file.** Prefer a flock()-guarded lock file, which the kernel releases automatically on process death.
- **Grep the whole tree before hiding a `_detail::` helper in an anonymous namespace.** Some "internal-looking" helpers are actually called directly from other modules or tests.
- **keqing-shell uses a separate `accentAlt` token for tile/chip selection borders, not `accent`.** `accent` is reserved for other UI elements like the nav rail and toggle track.
- **A ported config header's constants must trace 1:1 to the QML source's actual values.** Invented "roughly similar" numbers drifted from real properties and missed computed geometry.
- **A generically-named `constexpr` constant can collide with an identical name in an unrelated header.** Two modules that never include each other can still land in the same translation unit transitively.
- **An include-path migration script must exclude generated protocol-header includes from rewriting.** They resolve as if under `src/` but are build-directory outputs, breaking only at compile time.
- **Launcher is split: `src/modules/launcher.cpp` is main-executable-only (EGL/GL/Wayland core), pure logic lives in `src/launcher/*` and is dual-compiled into both `kokusei` and `kokusei-test`.** A `src/launcher/*` file still can't gain a `WaylandState`-typed function - the constraint that used to be enforced by convention alone (everything lived in one dual-compiled TU) is now enforced by the file boundary itself; keep such logic (e.g. an IPC verb needing full app state) in the app-level caller instead.
- **A module can't include another module's header, and `kokusei.cpp` can't name a module's function directly (`local/important/convention.md`).** Cross-module orchestration (the IPC verb table, the keyboard-focus dispatch table) lives in `src/app/` instead, which both modules and `kokusei.cpp` may depend on.
- **A shared helper that drifted into one feature module's directory pulls every later caller across the module boundary with it.** `spawn_detached` sat in `src/launcher/`, `resolve_app_icon_path` sat in `src/launcher/`, both had no launcher-specific logic; moving each to `src/core/` and `src/service/` (both allowed everywhere) fixed every caller at once instead of patching each include site.
- **A generic dispatcher needing another module's `open` flag should take a `bool`, not the module's full state struct.** `bar_detail::panel_pill()` took `const StarwardState&`/`const ControlCenterState&` just to read two fields; the only caller already had those modules' full types via `src/app/`, so it resolves the bools itself and passes primitives in, keeping the dispatcher's own module boundary clean.
- **An enum shared between a module and its infrastructure-layer consumer belongs in `config/`, not in the module's own header.** `SettingsFieldId` lived in `src/modules/settings.h`, forcing `src/service/settings_service.cpp` to include the whole module just for the enum; moving it to `src/config/settings_config.h` (already an allowed dependency for everyone) let the service keep depending only on infrastructure.
- **A pure-logic function needed by a second feature module should move to `service/`, even mid-file with private helpers.** `wallpaper_decode_scaled` and its cache/downsample helpers lived in `src/modules/wallpaper.cpp` only because the wallpaper module needed them first; the settings wallpaper-picker tab needing the same decode forced a cross-module include until the whole cluster moved to `src/service/wallpaper_service.cpp`.
- **A `grep -rln '#include "modules/'` sweep across the whole tree catches violations a targeted per-file review misses.** The first pass on this refactor found 4 violations by reasoning about known-suspect files; a full-tree grep afterward turned up 2 more (`settings/wallpaper_tab.cpp`, a dead include in `test/core/test_async_process.cpp`) that weren't in the suspect list.
