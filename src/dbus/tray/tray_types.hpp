#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct TrayItem {
    std::string bus_name;
    std::string object_path;
    std::string icon_name;
    std::string icon_theme_path;
    std::string status;
    std::string menu_object_path;
    bool has_menu = false;

    std::string key() const { return bus_name + "|" + object_path; }
};

struct MenuEntry {
    int32_t id = 0;
    std::string label;
    bool enabled = true;
    bool visible = true;
    bool is_separator = false;
    bool is_checkbox = false;
    bool checked = false;
    std::vector<MenuEntry> children;
};
