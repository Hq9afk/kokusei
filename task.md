# toml-config to json-config — execution checklist

Source plan: `local/plan/toml-to-json-config.md`.

- [x] `src/app/config.cpp` — rewritten on `nlohmann::json` (was `toml++`)
- [x] `meson.build` — dropped `tomlplusplus_dep` everywhere, added the
      missing `nlohmann_json_dep` to the test executable
- [x] `readme.md` — dropped `tomlplusplus` from the pacman package list
- [x] `test/app/test_config.cpp` — path/content assertions moved to JSON
- [x] `important/structure.md` — `config.h`+`.cpp` line now says JSON
- [x] Build verification (`dist/test.sh`, exit 0)
- [x] `~/.config/kokusei/config.json` seeded from the real device
      `config.toml`; old `config.toml` deleted

## Deviations from the written plan (judgment call, asked about)

- `autohide` moved from a bare top-level key to `bar.autohideEnabled`,
  matching `keqing-shell`'s naming for the same concept, after the user
  asked to check whether kokusei's schema could match `keqing-shell`'s.
  The rest of `keqing-shell`'s schema (`dock`, `osd.active`,
  `powerButtons`) has no kokusei equivalent (no dock module, no runtime
  OSD-source filter, no configurable power-button list) and was not added.
