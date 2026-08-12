#include "bluetooth_service.h"

#include "../../core/log.h"
#include "../../system/rfkill.h"

#include <map>
#include <optional>

namespace bluetooth_detail {

constexpr const char *kService = "org.bluez";
constexpr const char *kRootPath = "/";
constexpr const char *kAdapterIface = "org.bluez.Adapter1";
constexpr const char *kDeviceIface = "org.bluez.Device1";
constexpr const char *kBatteryIface = "org.bluez.Battery1";
constexpr const char *kObjectManagerIface =
    "org.freedesktop.DBus.ObjectManager";

using InterfaceProps = std::map<std::string, sdbus::Variant>;
using ObjectInterfaces = std::map<std::string, InterfaceProps>;
using ManagedObjects = std::map<sdbus::ObjectPath, ObjectInterfaces>;

template <typename T> std::optional<T> variant_get(const sdbus::Variant &v) {
    try {
        return v.get<T>();
    } catch (const sdbus::Error &) {
        return std::nullopt;
    }
}

void read_adapter_props(const InterfaceProps &props, BluetoothState &state) {
    state.adapter_present = true;
    if (auto it = props.find("Powered"); it != props.end()) {
        if (auto v = variant_get<bool>(it->second))
            state.powered = *v;
    }
    if (auto it = props.find("Discovering"); it != props.end()) {
        if (auto v = variant_get<bool>(it->second))
            state.scanning = *v;
    }
}

void merge_device_props(const InterfaceProps &props, BluetoothDeviceInfo &out) {
    if (auto it = props.find("Address"); it != props.end())
        if (auto v = variant_get<std::string>(it->second))
            out.address = *v;
    if (auto it = props.find("Alias"); it != props.end()) {
        if (auto v = variant_get<std::string>(it->second))
            out.name = *v;
    } else if (auto it2 = props.find("Name"); it2 != props.end()) {
        if (auto v = variant_get<std::string>(it2->second))
            out.name = *v;
    }
    if (auto it = props.find("Paired"); it != props.end())
        if (auto v = variant_get<bool>(it->second))
            out.paired = *v;
    if (auto it = props.find("Trusted"); it != props.end())
        if (auto v = variant_get<bool>(it->second))
            out.trusted = *v;
    if (auto it = props.find("Connected"); it != props.end()) {
        if (auto v = variant_get<bool>(it->second)) {
            out.connected = *v;
            if (out.connected)
                out.connecting = false;
            else {
                out.has_battery = false;
                out.battery_percent = 0;
            }
        }
    }
    BluetoothDeviceKind kind_from_icon = BluetoothDeviceKind::Unknown;
    if (auto it = props.find("Icon"); it != props.end()) {
        if (auto v = variant_get<std::string>(it->second))
            kind_from_icon = classify_icon(*v);
    }
    if (kind_from_icon != BluetoothDeviceKind::Unknown) {
        out.kind = kind_from_icon;
    } else if (auto it = props.find("Class"); it != props.end()) {
        if (auto v = variant_get<uint32_t>(it->second)) {
            BluetoothDeviceKind kind_from_cod = classify_class(*v);
            if (kind_from_cod != BluetoothDeviceKind::Unknown)
                out.kind = kind_from_cod;
        }
    }
    if (out.name.empty())
        out.name = out.address.empty() ? "Unknown Device" : out.address;
}

void merge_battery_props(const InterfaceProps &props,
                         BluetoothDeviceInfo &out) {
    if (!out.connected) {
        out.has_battery = false;
        out.battery_percent = 0;
        return;
    }
    if (auto it = props.find("Percentage"); it != props.end()) {
        if (auto v = variant_get<uint8_t>(it->second)) {
            out.has_battery = true;
            out.battery_percent = *v;
        }
    }
}

BluetoothDeviceInfo *find_device(BluetoothState &state,
                                 const std::string &path) {
    for (BluetoothDeviceInfo &d : state.devices)
        if (d.path == path)
            return &d;
    return nullptr;
}

std::string connected_device_path(const BluetoothState &state) {
    for (const BluetoothDeviceInfo &d : state.devices)
        if (d.connected)
            return d.path;
    return "";
}

void notify_connection_change(BluetoothState &state,
                              const BluetoothNotifyFn &notify) {
    std::string cur = connected_device_path(state);
    if (cur == state.prev_connected_path)
        return;
    if (state.init_done && notify) {
        if (!cur.empty()) {
            const BluetoothDeviceInfo *d = find_device(state, cur);
            notify("Connected", "Connected to " + (d ? d->name : cur));
        } else if (!state.prev_connected_path.empty()) {
            const BluetoothDeviceInfo *d =
                find_device(state, state.prev_connected_path);
            notify("Disconnected",
                   "Disconnected from " +
                       (d ? d->name : state.prev_connected_path));
        }
    }
    state.prev_connected_path = cur;
}

void apply_managed_objects(BluetoothState &state, const ManagedObjects &objects,
                           const BluetoothNotifyFn &notify) {
    std::vector<BluetoothDeviceInfo> next_devices;
    bool adapter_found = false;

    for (const auto &[path, interfaces] : objects) {
        if (auto it = interfaces.find(kAdapterIface); it != interfaces.end()) {
            if (!adapter_found) {
                adapter_found = true;
                state.adapter_path = path;
                read_adapter_props(it->second, state);
            }
        }
    }
    if (!adapter_found) {
        state.adapter_present = false;
        state.adapter_path.clear();
        state.adapter.reset();
    }

    for (const auto &[path, interfaces] : objects) {
        auto dev_it = interfaces.find(kDeviceIface);
        if (dev_it == interfaces.end())
            continue;
        BluetoothDeviceInfo info;
        info.path = path;
        merge_device_props(dev_it->second, info);
        if (auto batt_it = interfaces.find(kBatteryIface);
            batt_it != interfaces.end())
            merge_battery_props(batt_it->second, info);
        next_devices.push_back(std::move(info));
    }
    state.devices = std::move(next_devices);
    notify_connection_change(state, notify);
}

sdbus::IProxy *adapter(BluetoothState &state) {
    if (!state.adapter_present)
        return nullptr;
    if (!state.adapter) {
        state.adapter = sdbus::createProxy(
            state.root->getConnection(), sdbus::ServiceName{kService},
            sdbus::ObjectPath{state.adapter_path});
    }
    return state.adapter.get();
}

} // namespace bluetooth_detail

bool bluetooth_init(BluetoothState &state, sdbus::IConnection &bus) {
    using namespace bluetooth_detail;
    try {
        state.root = sdbus::createProxy(bus, sdbus::ServiceName{kService},
                                        sdbus::ObjectPath{kRootPath});

        state.root->uponSignal("InterfacesAdded")
            .onInterface(kObjectManagerIface)
            .call(
                [&state](const sdbus::ObjectPath &, const ObjectInterfaces &) {
                    state.next_refresh_at = std::chrono::steady_clock::now();
                });
        state.root->uponSignal("InterfacesRemoved")
            .onInterface(kObjectManagerIface)
            .call([&state](const sdbus::ObjectPath &,
                           const std::vector<std::string> &) {
                state.next_refresh_at = std::chrono::steady_clock::now();
            });

        ManagedObjects objects;
        state.root->callMethod("GetManagedObjects")
            .onInterface(kObjectManagerIface)
            .storeResultsTo(objects);
        apply_managed_objects(state, objects, nullptr);
        state.init_done = true;

        klog("bluetooth: connected, adapter_present=%d powered=%d",
             state.adapter_present, state.powered);
        return true;
    } catch (const sdbus::Error &e) {
        klog("bluetooth: connection failed (%s): %s - no bluetooth info "
             "available",
             e.getName().c_str(), e.getMessage().c_str());
        state.root.reset();
        return false;
    }
}

void bluetooth_set_powered(BluetoothState &state, bool enabled) {
    sdbus::IProxy *adapter = bluetooth_detail::adapter(state);
    if (!adapter)
        return;

    if (enabled) {
        if (rfkill_bluetooth_hard_blocked()) {
            klog("bluetooth: setPowered: rfkill hard block is active");
            return;
        }
        if (!rfkill_set_bluetooth_soft_blocked(false))
            klog("bluetooth: rfkill unblock failed, trying Powered anyway");
    }
    try {

        adapter->setProperty("Powered")
            .onInterface(bluetooth_detail::kAdapterIface)
            .toValue(enabled, sdbus::dont_expect_reply);
        state.next_refresh_at = std::chrono::steady_clock::now();
    } catch (const sdbus::Error &e) {
        klog("bluetooth: setPowered failed (%s): %s", e.getName().c_str(),
             e.getMessage().c_str());
    }
}

void bluetooth_start_discovery(BluetoothState &state) {
    sdbus::IProxy *adapter = bluetooth_detail::adapter(state);
    if (!adapter)
        return;
    try {
        adapter->callMethodAsync("StartDiscovery")
            .onInterface(bluetooth_detail::kAdapterIface)
            .uponReplyInvoke([](std::optional<sdbus::Error> err) {
                if (err)
                    klog("bluetooth: StartDiscovery failed: %s",
                         err->getMessage().c_str());
            });
    } catch (const sdbus::Error &e) {
        klog("bluetooth: StartDiscovery dispatch failed: %s",
             e.getMessage().c_str());
    }
}

void bluetooth_stop_discovery(BluetoothState &state) {
    sdbus::IProxy *adapter = bluetooth_detail::adapter(state);
    if (!adapter)
        return;
    try {
        adapter->callMethodAsync("StopDiscovery")
            .onInterface(bluetooth_detail::kAdapterIface)
            .uponReplyInvoke([](std::optional<sdbus::Error> err) {
                if (err)
                    klog("bluetooth: StopDiscovery failed: %s",
                         err->getMessage().c_str());
            });
    } catch (const sdbus::Error &e) {
        klog("bluetooth: StopDiscovery dispatch failed: %s",
             e.getMessage().c_str());
    }
}

void bluetooth_connect(BluetoothState &state, const std::string &device_path) {
    if (BluetoothDeviceInfo *d =
            bluetooth_detail::find_device(state, device_path))
        d->connecting = true;
    try {
        auto proxy =
            sdbus::createProxy(state.root->getConnection(),
                               sdbus::ServiceName{bluetooth_detail::kService},
                               sdbus::ObjectPath{device_path});
        proxy->callMethodAsync("Connect")
            .onInterface(bluetooth_detail::kDeviceIface)
            .uponReplyInvoke([](std::optional<sdbus::Error> err) {
                if (err)
                    klog("bluetooth: Connect failed: %s",
                         err->getMessage().c_str());
            });
    } catch (const sdbus::Error &e) {
        klog("bluetooth: Connect dispatch failed: %s", e.getMessage().c_str());
    }
}

void bluetooth_disconnect(BluetoothState &state,
                          const std::string &device_path) {
    try {
        auto proxy =
            sdbus::createProxy(state.root->getConnection(),
                               sdbus::ServiceName{bluetooth_detail::kService},
                               sdbus::ObjectPath{device_path});
        proxy->callMethodAsync("Disconnect")
            .onInterface(bluetooth_detail::kDeviceIface)
            .uponReplyInvoke([](std::optional<sdbus::Error> err) {
                if (err)
                    klog("bluetooth: Disconnect failed: %s",
                         err->getMessage().c_str());
            });
    } catch (const sdbus::Error &e) {
        klog("bluetooth: Disconnect dispatch failed: %s",
             e.getMessage().c_str());
    }
}

void bluetooth_pair(BluetoothState &state, const std::string &device_path) {
    if (BluetoothDeviceInfo *d =
            bluetooth_detail::find_device(state, device_path))
        d->connecting = true;
    try {
        auto proxy =
            sdbus::createProxy(state.root->getConnection(),
                               sdbus::ServiceName{bluetooth_detail::kService},
                               sdbus::ObjectPath{device_path});
        proxy->callMethodAsync("Pair")
            .onInterface(bluetooth_detail::kDeviceIface)
            .uponReplyInvoke([](std::optional<sdbus::Error> err) {
                if (err)
                    klog("bluetooth: Pair failed: %s",
                         err->getMessage().c_str());
            });
    } catch (const sdbus::Error &e) {
        klog("bluetooth: Pair dispatch failed: %s", e.getMessage().c_str());
    }
}

void bluetooth_forget(BluetoothState &state, const std::string &device_path) {
    sdbus::IProxy *adapter = bluetooth_detail::adapter(state);
    if (!adapter)
        return;
    try {
        adapter->callMethodAsync("RemoveDevice")
            .onInterface(bluetooth_detail::kAdapterIface)
            .withArguments(sdbus::ObjectPath{device_path})
            .uponReplyInvoke([](std::optional<sdbus::Error> err) {
                if (err)
                    klog("bluetooth: RemoveDevice failed: %s",
                         err->getMessage().c_str());
            });
    } catch (const sdbus::Error &e) {
        klog("bluetooth: RemoveDevice dispatch failed: %s",
             e.getMessage().c_str());
    }
}

void bluetooth_tick(BluetoothState &state, const BluetoothNotifyFn &notify,
                    std::chrono::steady_clock::time_point now,
                    std::function<void()> on_changed) {
    if (!state.root || now < state.next_refresh_at)
        return;
    state.next_refresh_at = now + std::chrono::seconds(1);

    state.root->callMethodAsync("GetManagedObjects")
        .onInterface(bluetooth_detail::kObjectManagerIface)
        .uponReplyInvoke([&state, notify, on_changed](
                             std::optional<sdbus::Error> err,
                             bluetooth_detail::ManagedObjects objects) {
            if (err) {
                klog("bluetooth: GetManagedObjects failed: %s",
                     err->getMessage().c_str());
                return;
            }
            bluetooth_detail::apply_managed_objects(state, objects, notify);
            if (on_changed)
                on_changed();
        });
}
