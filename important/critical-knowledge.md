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
- **A filled widget drawn at the same origin as an earlier label silently paints over it.** `visualizer_tab.cpp`'s two shape tiles both started at the label's own `(x,y)`, hiding it under the first tile.
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
- **A custom multi-pass GPU shader effect bypasses the Node/Scene graph entirely.** Give it its own programs/FBOs, called directly from the module's paint function.
- **GLSL ES 1.00's `for` loop bound must be a compile-time constant, not a uniform.** A tunable sample-count `#define` must stay a literal `const`, not a runtime uniform.
- **A per-frame multi-tap fullscreen shader pass at native output resolution can stall the shared single poll loop.** The sphere shape's 96-tap glow blur froze the shell until moved to a downsampled FBO.
- **A per-frame `glGenBuffers`/`glDeleteBuffers` for a static quad is driver churn every other buffer in the codebase avoids.** Create once alongside the program/VBO it belongs to, like `Renderer::quad_vbo_`.
- **Resolution/tap-count tuning alone couldn't stop `sphere` from stalling the poll loop; it still ran synchronously.** The visualizer window now runs on its own thread with its own share-context `EGLContext`; every other surface stays shared.
- **A dedicated render thread sharing an `EGLSurface` with the main thread must never be current on it simultaneously.** The render thread owns the surface while open; the main thread only hands off a per-frame struct under a mutex.
- **Two hand-rolled opacity pipelines for two shapes of the same window drift apart silently.** Per-shape special-casing let sphere's opacity ignore fade-in; unifying onto one shared Renderer/Scene path fixed it.
- **A single shared `Renderer` needs its own reset at frame start, not caller discipline.** Callers leaked stale opacity into the next paint; `Renderer::begin_frame()` now resets it itself, fixing every caller.
- **A freshly created share-context starts with `GL_BLEND` disabled, even sharing a namespace with a context that has it enabled.** A raw GL-passes-only path (sphere_visualizer) must call `glEnable(GL_BLEND)` itself once per context.
- **`glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)` squares alpha when a translucent rect draws over a transparent-cleared surface.** Use `glBlendFuncSeparate` with `GL_ONE` for the alpha channel so alpha lands exactly where set.
- **A reference shader's final coordinate remap is easy to drop when porting the math piecewise.** `ncs/1.frag`'s last line squeezes each particle's position by `(coords+center)/2`; omitting it rendered the sphere off-window.
- **Two differently-tuned copies of the same NCS shader exist; the wrong one's constants still compile, just wrong.** Sphere's constants came from `ncs-spectrum-glava` instead of `wayves`, off 100x+ on flow speed and colors.
- **A visual's dedicated file owning raw GL state can end up asymmetric with a sibling sharing the host's state.** Splitting bar's inline logic into its own file surfaced a missing `Renderer::destroy()`, fixing a leaked GL teardown.
- **Renaming a persisted config value needs a load-time compatibility mapping, or existing users silently lose their setting.** `visualizer_shape`'s strings changed `bars`/`ncs`→`bar`/`sphere`; `config.cpp` maps old to new after `value_or()`.
- **An AviUtl custom-object `.obj` script is Shift-JIS text, not binary.** It reads as binary garbage to a UTF-8 reader; `iconv -f SHIFT-JIS -t UTF-8` turns it into ordinary Lua.
- **Two prior sphere references both ported the same 2D noise-plus-pull-to-circle effect, not an actual sphere.** Retuning constants couldn't fix structurally wrong math; the reference was swapped to `ncs4au`'s real algorithm.
- **A uniform grid projected orthographically onto a sphere brightens at its own silhouette, no separate ring effect needed.** Points-per-pixel diverges near the limb; `ncs4au`'s fold-back (`z=abs(z)`) plus glow should reproduce the dot-ring look.

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
- **A struct member can't share a name with a Wayland protocol type used in the same header.** `xdg_surface *xdg_surface` compiles but breaks name lookup in including translation units; rename the field.
- **A real `xdg_toplevel` window is created on open and destroyed on close, not kept mapped-but-transparent.** Unlike a layer-shell overlay's fade-in-place trick, a zero-opacity mapped toplevel would still show in switchers.
- **`ToplevelWindowBase` has no generic resize callback, only `on_close_request`.** A module keeping a persistent per-size buffer must compare against live width/height each paint, or a resize stretches stale content.
- **An animation's `on_complete` that destroys the surface it's animating can fire mid-frame, inside the paint function's own `tick()` call.** Re-check surface validity right after `tick()`; where paint isn't re-checked, defer the destroy to the next poll iteration.
- **A layer-shell overlay now destroys its surface on close and lazily recreates it on next open, not staying mapped.** `overlay_panel_toggle` handles most consumers; a closed panel now costs no GPU buffers.
- **Tearing down a `ToplevelWindowBase` must also reset its `FrameClock` fields, not just the Wayland/EGL handles.** A stale non-null `frame_clock.callback` makes `request_frame` silently no-op forever afterward.
- **A pending `wl_callback` must be released with `wl_callback_destroy`, never just null the pointer.** `wl_surface_destroy` doesn't free it; a late `done` event then fires against a reused, reset struct.
- **Matrix and visualizer are meant to tile as regular windows, not float.** Do not add a `float = true` window rule for these; that's a rejected direction, not an oversight.
- **A destroy-on-close `xdg_toplevel` can hand its next window handle the exact address a prior instance had.** Per-window state keyed by address must clear when the window goes away, or a reopened window inherits stale state.
- **In `hl`'s Lua event API, a window's fields are only safe to read on `window.close`, not `window.destroy`.** By `window.destroy` fields read back `nil`; assigning to a nil table key crashes Lua.

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
- **A hover-driven highlight must clear on lost surface focus, not just recompute on motion.** Another surface stealing pointer focus mid-hover leaves a stale hovered index unless explicitly cleared.
- **A mutex must cover the read side of a shared buffer, not just the write side.** Per-channel splitting moved ring-buffer reads outside `ring_mutex_` while the PipeWire thread still wrote under it.
- **A sync-from-config function uploading only on non-empty paths must also explicitly clear the texture when the path becomes empty.** The fix resets the column's `Texture` and bumps its generation counter to reject stale decodes.
- **Clearing a texture in memory doesn't repaint the surface, only uploading one does, unless both call `wallpaper_request_frame`.** The empty-path clear branch skipped that call, so the compositor kept showing the old wallpaper.

## 6. Architecture and scale discipline

- **noctalia is a reference for ideas, not a template to copy wholesale.** It's roughly 45x kokusei's size; every adopted idea must be resized to kokusei's scale.
- **Several noctalia subsystems were deliberately skipped, not overlooked.** A retained scene graph, backend abstraction, and scripting engine solve problems kokusei doesn't have.
- **Config hot-reload was built, but schema-validated multi-file config remains rejected.** Only when the single file is read changed, not its structure.
- **A config field lacking settings UI may need deletion, not a new control.** Check the reference project first; it may hardcode the same value with no UI either.
- **noctalia's testing philosophy and logic/UI separation already matched kokusei's existing convention.** Naming what was already right is as important as naming what needs to change.
- **Check a reference technique against the full target hardware range, not one profiling machine.** An integrated-GPU-only measurement wrongly justified diverging from a technique discrete GPUs need.
- **Animated wallpaper decode now runs in-process via `libavcodec`/`libavfilter`, not a spawned `ffmpeg` per column.** Replaced 3 duplicated `ffmpeg` processes plus a raw-rgba pipe with one shared decode loop.
- **Hardware decoder selection stays portable across GPU vendors by trying a preference list of `AVHWDeviceType`s, not by branching on hardware.** Tries `CUDA` then `VAAPI`, falling through to software.
- **A GPU zero-copy texture import mechanism is vendor-specific, not portable across driver stacks, unlike hardware decode selection above.** `VAAPI`'s `DMA-BUF`/`EGLImage` trick needs Mesa; `CUDA`-GL interop is a separate NVIDIA mechanism.
- **`AVCodecContext::get_format` can't capture lambda state, only a plain function pointer.** `wallpaper_hw_decode.cpp` passes the wanted hardware pixel format through `codec_ctx->opaque` instead.
- **A decode filter graph can't be built before the first frame when the hardware transfer format isn't known upfront.** `CUDA`/`VAAPI` transfer format varies by driver; the graph builds from the first decoded frame instead.
- **Looping in-process decoded video needs a seek-and-flush, not a process restart.** `av_seek_frame` plus `avcodec_flush_buffers` on EOF replaces the `ffmpeg` CLI's loop flag.
- **A decoder can hold a frame back internally, released only by the next `send_packet` or an explicit flush.** Flushing before draining drops it; fix sends a nullptr flush packet and drains first.
- **A reference's per-output design choice can be a consequence of threading, not correctness.** kokusei is single-threaded, needing one shared EGLContext; the visualizer window is a scoped, authorized exception.
- **`noctalia`'s render architecture is one thread for all GL/scene work, every style an ordinary `Node` sharing one opacity pipeline.** kokusei's earlier sphere-only special-case caused divergence; one thread for both shapes now matches noctalia's model.
- **An abstraction earns its place only by removing duplication that exists today.** A planned compositor-backend interface was dropped once its motivating duplication was already merged away.
- **Build a generic primitive only once a second real caller is visible, not imaginable.** `DeferredCall` ended up with no caller and stays documented rather than wired in.
- **A default-plus-override config value must be cached on the consumer's own per-monitor state.** Re-resolving the tier chain on every hot-path read would turn 15 reads into map lookups.
- **A resolved-with-fallback accessor and a raw-override accessor answer different questions and must not be interchanged.** A "remove override" control needs the raw override only; using the fallback resolver there makes it no-op wrongly.
- **Porting a singleton overlay to per-monitor rendering should split process-wide from per-monitor state.** A D-Bus connection is one-per-process; the render surface is one-per-monitor, don't duplicate both.
- **A cross-cutting service with no surface of its own needs a `Service`, not a `Module`.** `Module`/`PerMonitorModule` own a surface; cross-cutting services get the same generic-loop treatment via `service.h`.
- **Extending a fallback-inclusive resolver to a second mode needs its own raw-override accessor too, or the same pitfall reappears.** Adding an animated-column fallback required a matching `_override` accessor.
- **A fallback gated on `column_index == 0` isn't global, it's whichever monitor resolves column 0 first.** A truly global toggle needs the same check on every column, everywhere.
- **Animated mode's empty-column default-wallpaper fallback reuses the static `wallpaper_path` image.** No separate default-animated-wallpaper asset exists; resolve any future one the same way, not a parallel field.
- **A per-monitor module's `create_surface()` must not gate on config state, since it runs once with no re-entry point.** Wallpaper's module gated surface creation on a startup check, breaking later toggle-on.

## 7. Build and workflow

- **A file writer must create its own target directory, not assume something else already did.** `write_file_atomic` silently failed `save_config()` on fresh installs; other writers already `mkdir()` first.
- **Only the user runs `dist/install` and `dist/run`.** Both perform real sudo actions or launch a live session; `dist/test` is safe to run freely.
- **Batch edits and build once, not after every small change.** Reformat with clang-format after each edit, keeping comments short enough not to wrap.
- **Bare `clang-format -i` silently reformats the whole file to 2-space LLVM default, not the project's 4-space style.** No `.clang-format` file exists; use `convention.md`'s documented `--style="{IndentWidth: 4}"` command.
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
- **Launcher is split: `launcher.cpp` is main-executable-only (EGL/GL/Wayland); pure logic in `src/launcher/*` is dual-compiled into both binaries.** A `src/launcher/*` file still can't gain a `WaylandState`-typed function; now enforced by the file boundary.
- **A module can't include another module's header, and `kokusei.cpp` can't name a module's function directly.** Cross-module orchestration (IPC verb table, key-dispatch table) lives in `src/app/` instead.
- **A shared helper that drifted into one feature module's directory pulls every later caller across the module boundary with it.** `spawn_detached`/`resolve_app_icon_path` had no launcher-specific logic; moving to `core/`/`service/` fixed every caller.
- **A generic dispatcher needing another module's `open` flag should take a `bool`, not the module's full state struct.** `panel_pill()` took full state structs just to read two fields; it now resolves bools itself.
- **An enum shared between a module and its infrastructure-layer consumer belongs in `config/`, not in the module's own header.** `SettingsFieldId` lived in `settings.h`, forcing the service to include the whole module; moved to `config/`.
- **A pure-logic function needed by a second feature module should move to `service/`, even mid-file with private helpers.** `wallpaper_decode_scaled` lived in `wallpaper.cpp` until the settings tab needed it too, forcing the move.
- **A `grep -rln '#include "modules/'` sweep across the whole tree catches violations a targeted per-file review misses.** A reasoning-based pass found 4 violations; a full-tree grep found 2 more.
