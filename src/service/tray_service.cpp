#include "service/tray_service.h"

#include "core/log.h"
#include "service/icon_theme.h"

#include <filesystem>
#include <map>
#include <optional>

namespace {

constexpr const char *kWatcherIface = "org.kde.StatusNotifierWatcher";
constexpr const char *kWatcherPath = "/StatusNotifierWatcher";
constexpr const char *kItemIface = "org.kde.StatusNotifierItem";
constexpr const char *kMenuIface = "com.canonical.dbusmenu";
constexpr const char *kPropsIface = "org.freedesktop.DBus.Properties";

using DBusMenuLayout =
    sdbus::Struct<int32_t, std::map<std::string, sdbus::Variant>,
                  std::vector<sdbus::Variant>>;

template <typename T> std::optional<T> variant_get(const sdbus::Variant &v) {
    try {
        return v.get<T>();
    } catch (const sdbus::Error &) {
        return std::nullopt;
    }
}

TrayItem *find_item(TrayState &state, const std::string &key) {
    for (TrayItem &item : state.items)
        if (item.key() == key)
            return &item;
    return nullptr;
}

void apply_item_props(TrayItem &item,
                      const std::map<std::string, sdbus::Variant> &props) {
    if (auto it = props.find("IconName"); it != props.end())
        if (auto v = variant_get<std::string>(it->second))
            item.icon_name = *v;
    if (auto it = props.find("IconThemePath"); it != props.end())
        if (auto v = variant_get<std::string>(it->second))
            item.icon_theme_path = *v;
    if (auto it = props.find("Status"); it != props.end())
        if (auto v = variant_get<std::string>(it->second))
            item.status = *v;
    if (auto it = props.find("Menu"); it != props.end())
        if (auto v = variant_get<sdbus::ObjectPath>(it->second)) {
            item.menu_object_path = *v;
            item.has_menu = !item.menu_object_path.empty();
        }
}

void watch_item_properties(TrayState &state, const std::string &key,
                           sdbus::IProxy &proxy) {
    proxy.uponSignal("PropertiesChanged")
        .onInterface(kPropsIface)
        .call(
            [&state, key](const std::string &,
                          const std::map<std::string, sdbus::Variant> &changed,
                          const std::vector<std::string> &) {
                TrayItem *item = find_item(state, key);
                if (!item)
                    return;
                apply_item_props(*item, changed);
                state.dirty = true;
            });
}

void register_item(TrayState &state, const std::string &bus_name,
                   const std::string &object_path) {
    std::string key = bus_name + "|" + object_path;
    if (find_item(state, key))
        return;

    TrayItem item;
    item.bus_name = bus_name;
    item.object_path = object_path;
    state.items.push_back(item);
    state.dirty = true;

    auto proxy = sdbus::createProxy(*state.bus, sdbus::ServiceName{bus_name},
                                    sdbus::ObjectPath{object_path});
    sdbus::IProxy *proxy_ptr = proxy.get();
    state.item_proxies[key] = std::move(proxy);
    watch_item_properties(state, key, *proxy_ptr);

    proxy_ptr->callMethodAsync("GetAll")
        .onInterface(kPropsIface)
        .withArguments(std::string(kItemIface))
        .uponReplyInvoke(
            [&state, key](std::optional<sdbus::Error> err,
                          std::map<std::string, sdbus::Variant> props) {
                if (err) {
                    klog("tray: GetAll failed for %s: %s", key.c_str(),
                         err->getMessage().c_str());
                    return;
                }
                TrayItem *item = find_item(state, key);
                if (!item)
                    return;
                apply_item_props(*item, props);
                state.dirty = true;
            });

    klog("tray: registered item %s", key.c_str());
}

MenuEntry parse_menu_node(const DBusMenuLayout &node) {
    MenuEntry entry;
    entry.id = std::get<0>(node);
    const std::map<std::string, sdbus::Variant> &props = std::get<1>(node);
    if (auto it = props.find("label"); it != props.end())
        entry.label = variant_get<std::string>(it->second).value_or("");
    if (auto it = props.find("enabled"); it != props.end())
        entry.enabled = variant_get<bool>(it->second).value_or(true);
    if (auto it = props.find("visible"); it != props.end())
        entry.visible = variant_get<bool>(it->second).value_or(true);
    if (auto it = props.find("type"); it != props.end())
        entry.is_separator =
            variant_get<std::string>(it->second).value_or("") == "separator";
    if (auto it = props.find("toggle-type"); it != props.end())
        entry.is_checkbox =
            !variant_get<std::string>(it->second).value_or("").empty();
    if (auto it = props.find("toggle-state"); it != props.end())
        entry.checked = variant_get<int32_t>(it->second).value_or(0) == 1;

    for (const sdbus::Variant &child : std::get<2>(node)) {
        try {
            entry.children.push_back(
                parse_menu_node(child.get<DBusMenuLayout>()));
        } catch (const sdbus::Error &) {
        }
    }
    return entry;
}

} // namespace

bool tray_init(TrayState &state) {
    try {
        state.bus = sdbus::createSessionBusConnection();
        state.watcher_object =
            sdbus::createObject(*state.bus, sdbus::ObjectPath{kWatcherPath});

        state.watcher_object
            ->addVTable(
                sdbus::registerMethod("RegisterStatusNotifierItem")
                    .implementedAs([&state](const std::string &service) {
                        std::string sender =
                            state.watcher_object->getCurrentlyProcessedMessage()
                                .getSender();
                        std::string object_path = service.starts_with('/')
                                                      ? service
                                                      : "/StatusNotifierItem";
                        register_item(state, sender, object_path);
                    }),
                sdbus::registerMethod("RegisterStatusNotifierHost")
                    .implementedAs([](const std::string &) {}),
                sdbus::registerProperty("RegisteredStatusNotifierItems")
                    .withGetter([&state]() -> std::vector<std::string> {
                        std::vector<std::string> out;
                        for (const TrayItem &item : state.items)
                            out.push_back(item.bus_name + item.object_path);
                        return out;
                    }),
                sdbus::registerProperty("IsStatusNotifierHostRegistered")
                    .withGetter([]() -> bool { return true; }))
            .forInterface(kWatcherIface);

        state.bus->requestName(sdbus::ServiceName{kWatcherIface});

        state.dbus_proxy = sdbus::createProxy(
            *state.bus, sdbus::ServiceName{"org.freedesktop.DBus"},
            sdbus::ObjectPath{"/org/freedesktop/DBus"});
        state.dbus_proxy->uponSignal("NameOwnerChanged")
            .onInterface("org.freedesktop.DBus")
            .call([&state](const std::string &name, const std::string &,
                           const std::string &new_owner) {
                if (!new_owner.empty())
                    return;
                std::erase_if(state.items, [&](const TrayItem &item) {
                    return item.bus_name == name;
                });
                std::erase_if(state.item_proxies, [&](const auto &kv) {
                    return kv.first.starts_with(name + "|");
                });
                std::erase_if(state.menu_cache, [&](const auto &kv) {
                    return kv.first.starts_with(name + "|");
                });
                state.dirty = true;
            });

        klog("tray: registered %s", kWatcherIface);
        return true;
    } catch (const sdbus::Error &e) {
        klog("tray: D-Bus registration failed (%s): %s - is another tray "
             "already running?",
             e.getName().c_str(), e.getMessage().c_str());
        state.bus.reset();
        return false;
    }
}

void tray_activate(TrayState &state, const TrayItem &item, bool secondary) {
    auto it = state.item_proxies.find(item.key());
    if (it == state.item_proxies.end())
        return;
    it->second->callMethodAsync(secondary ? "SecondaryActivate" : "Activate")
        .onInterface(kItemIface)
        .withArguments(int32_t{0}, int32_t{0})
        .uponReplyInvoke([](std::optional<sdbus::Error> err) {
            if (err)
                klog("tray: Activate failed: %s", err->getMessage().c_str());
        });
}

void tray_menu_request(TrayState &state, const TrayItem &item,
                       std::function<void()> on_ready) {
    if (!item.has_menu)
        return;
    try {
        auto menu_proxy =
            sdbus::createProxy(*state.bus, sdbus::ServiceName{item.bus_name},
                               sdbus::ObjectPath{item.menu_object_path});
        sdbus::IProxy *proxy_ptr = menu_proxy.get();

        proxy_ptr->callMethodAsync("AboutToShow")
            .onInterface(kMenuIface)
            .withArguments(int32_t{0})
            .uponReplyInvoke([](std::optional<sdbus::Error>) {});

        std::string key = item.key();
        auto owned_proxy =
            std::shared_ptr<sdbus::IProxy>(std::move(menu_proxy));
        proxy_ptr->callMethodAsync("GetLayout")
            .onInterface(kMenuIface)
            .withArguments(int32_t{0}, int32_t{-1}, std::vector<std::string>{})
            .uponReplyInvoke([&state, key, on_ready,
                              owned_proxy](std::optional<sdbus::Error> err,
                                           uint32_t, DBusMenuLayout layout) {
                if (err) {
                    klog("tray: GetLayout failed for %s: %s", key.c_str(),
                         err->getMessage().c_str());
                    return;
                }
                MenuEntry root = parse_menu_node(layout);
                state.menu_cache[key] = std::move(root.children);
                if (on_ready)
                    on_ready();
            });
    } catch (const sdbus::Error &e) {
        klog("tray: menu request failed: %s", e.getMessage().c_str());
    }
}

const std::vector<MenuEntry> *tray_menu_cached(const TrayState &state,
                                               const std::string &key) {
    auto it = state.menu_cache.find(key);
    return it != state.menu_cache.end() ? &it->second : nullptr;
}

void tray_menu_event_clicked(TrayState &state, const TrayItem &item,
                             int32_t entry_id) {
    try {
        auto proxy =
            sdbus::createProxy(*state.bus, sdbus::ServiceName{item.bus_name},
                               sdbus::ObjectPath{item.menu_object_path});
        sdbus::IProxy *proxy_ptr = proxy.get();
        auto owned_proxy = std::shared_ptr<sdbus::IProxy>(std::move(proxy));
        proxy_ptr->callMethodAsync("Event")
            .onInterface(kMenuIface)
            .withArguments(entry_id, std::string("clicked"),
                           sdbus::Variant(std::string()), uint32_t{0})
            .uponReplyInvoke([owned_proxy](std::optional<sdbus::Error> err) {
                if (err)
                    klog("tray: menu Event failed: %s",
                         err->getMessage().c_str());
            });
    } catch (const sdbus::Error &e) {
        klog("tray: menu Event dispatch failed: %s", e.getMessage().c_str());
    }
}

std::string tray_item_icon_path(const TrayItem &item) {
    if (item.icon_name.empty())
        return "";
    if (!item.icon_theme_path.empty()) {
        std::string candidate =
            item.icon_theme_path + "/" + item.icon_name + ".png";
        if (std::filesystem::exists(candidate))
            return candidate;
    }
    return resolve_app_icon_path(item.icon_name);
}
