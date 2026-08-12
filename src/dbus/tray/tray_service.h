#pragma once

#include "tray_types.h"
#include <sdbus-c++/sdbus-c++.h>

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

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
