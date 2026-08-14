#pragma once

#include <sdbus-c++/sdbus-c++.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
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

struct TrayState {
    std::unique_ptr<sdbus::IConnection> bus;
    std::unique_ptr<sdbus::IObject> watcher_object;
    std::unique_ptr<sdbus::IProxy> dbus_proxy;
    std::vector<TrayItem> items;
    std::unordered_map<std::string, std::unique_ptr<sdbus::IProxy>>
        item_proxies;
    std::unordered_map<std::string, std::vector<MenuEntry>> menu_cache;
    bool dirty = false;
};

bool tray_init(TrayState &state);

void tray_activate(TrayState &state, const TrayItem &item, bool secondary);

void tray_menu_request(TrayState &state, const TrayItem &item,
                       std::function<void()> on_ready);

const std::vector<MenuEntry> *tray_menu_cached(const TrayState &state,
                                               const std::string &key);

void tray_menu_event_clicked(TrayState &state, const TrayItem &item,
                             int32_t entry_id);

std::string tray_item_icon_path(const TrayItem &item);
