# Kokusei

## Important

- Supported Display Server: Wayland
- Supported Compositors: ShojiWM, Hyprland

## Prerequisite

**Arch**

```bash
sudo pacman -Syu --needed base-devel meson ninja mesa wayland wayland-protocols libxkbcommon sdbus-cpp freetype2 fontconfig cairo pango harfbuzz glib2 libsecret libsodium polkit pipewire wireplumber curl libqalculate libxml2 md4c nlohmann-json tomlplusplus libical jemalloc stb ffmpeg
```

## Installation

```bash
git clone https://github.com/Hq9afk/kokusei.git
cd kokusei
./dist/install.sh
```

## Running the shell

**Regular mode**
```bash
kokusei
```

**Debug mode**
```bash
kokusei debug
```
Or run kokusei in regular mode and read the logs at `~/.local/state/kokusei/kokusei.log`
