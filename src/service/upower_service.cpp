#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

#include "core/log.h"

#include "service/upower_service.h"

namespace {

enum class UpowerDeviceState : uint32_t {
    Charging = 1,
    FullyCharged = 4,
};

constexpr const char *kService = "org.freedesktop.UPower";
constexpr const char *kManagerPath = "/org/freedesktop/UPower";
constexpr const char *kManagerIface = "org.freedesktop.UPower";
constexpr const char *kDeviceIface = "org.freedesktop.UPower.Device";
constexpr const char *kPropertiesIface = "org.freedesktop.DBus.Properties";

void refresh(UpowerState &state) {
    if (!state.device)
        return;
    try {
        bool present = state.device->getProperty("IsPresent")
                           .onInterface(kDeviceIface)
                           .get<bool>();
        uint32_t raw_state = state.device->getProperty("State")
                                 .onInterface(kDeviceIface)
                                 .get<uint32_t>();
        double pct = state.device->getProperty("Percentage")
                         .onInterface(kDeviceIface)
                         .get<double>();

        bool charging =
            raw_state == static_cast<uint32_t>(UpowerDeviceState::Charging);
        bool full =
            raw_state == static_cast<uint32_t>(UpowerDeviceState::FullyCharged);
        int percent = static_cast<int>(std::lround(pct));

        if (present != state.present || charging != state.charging ||
            full != state.full || percent != state.percent) {
            state.dirty = true;
        }
        state.present = present;
        state.charging = charging;
        state.full = full;
        state.percent = percent;
    } catch (const sdbus::Error &e) {
        klog("upower: property read failed (%s): %s", e.getName().c_str(),
             e.getMessage().c_str());
    }
}

void refresh_device(UpowerState &state, UpowerDeviceEntry &entry) {
    if (!entry.proxy)
        return;
    try {
        entry.type = entry.proxy->getProperty("Type")
                         .onInterface(kDeviceIface)
                         .get<uint32_t>();
        entry.present = entry.proxy->getProperty("IsPresent")
                            .onInterface(kDeviceIface)
                            .get<bool>();
        entry.state = entry.proxy->getProperty("State")
                          .onInterface(kDeviceIface)
                          .get<uint32_t>();
        entry.percent =
            static_cast<int>(std::lround(entry.proxy->getProperty("Percentage")
                                             .onInterface(kDeviceIface)
                                             .get<double>()));
        entry.time_to_empty_s =
            static_cast<int>(entry.proxy->getProperty("TimeToEmpty")
                                 .onInterface(kDeviceIface)
                                 .get<int64_t>());
        entry.time_to_full_s =
            static_cast<int>(entry.proxy->getProperty("TimeToFull")
                                 .onInterface(kDeviceIface)
                                 .get<int64_t>());
        entry.native_path = entry.proxy->getProperty("NativePath")
                                .onInterface(kDeviceIface)
                                .get<std::string>();
        state.dirty = true;
    } catch (const sdbus::Error &e) {
        klog("upower: device property read failed (%s): %s",
             e.getName().c_str(), e.getMessage().c_str());
    }
}

void refresh_on_battery(UpowerState &state) {
    if (!state.manager)
        return;
    try {
        state.on_battery = state.manager->getProperty("OnBattery")
                               .onInterface(kManagerIface)
                               .get<bool>();
        state.dirty = true;
    } catch (const sdbus::Error &e) {
        klog("upower: OnBattery read failed (%s): %s", e.getName().c_str(),
             e.getMessage().c_str());
    }
}

void register_device(UpowerState &state, const std::string &path) {
    for (const auto &existing : state.devices)
        if (existing->path == path)
            return;

    auto entry = std::make_unique<UpowerDeviceEntry>();
    entry->path = path;
    try {
        entry->proxy = sdbus::createProxy(
            *state.bus, sdbus::ServiceName{kService}, sdbus::ObjectPath{path});
    } catch (const sdbus::Error &e) {
        klog("upower: failed to create proxy for %s: %s", path.c_str(),
             e.getMessage().c_str());
        return;
    }

    UpowerDeviceEntry *slot = entry.get();
    state.devices.push_back(std::move(entry));
    refresh_device(state, *slot);
    slot->proxy->uponSignal("PropertiesChanged")
        .onInterface(kPropertiesIface)
        .call([&state, slot](const std::string &,
                             const std::map<std::string, sdbus::Variant> &,
                             const std::vector<std::string> &) {
            refresh_device(state, *slot);
        });
}

} // namespace

bool upower_init(UpowerState &state) {
    try {
        state.bus = sdbus::createSystemBusConnection();
        state.manager =
            sdbus::createProxy(*state.bus, sdbus::ServiceName{kService},
                               sdbus::ObjectPath{kManagerPath});

        sdbus::ObjectPath device_path;
        state.manager->callMethod("GetDisplayDevice")
            .onInterface(kManagerIface)
            .storeResultsTo(device_path);

        state.device = sdbus::createProxy(
            *state.bus, sdbus::ServiceName{kService}, device_path);

        state.device->uponSignal("PropertiesChanged")
            .onInterface(kPropertiesIface)
            .call(
                [&state](const std::string &,
                         const std::map<std::string, sdbus::Variant> &,
                         const std::vector<std::string> &) { refresh(state); });

        std::vector<sdbus::ObjectPath> device_paths;
        state.manager->callMethod("EnumerateDevices")
            .onInterface(kManagerIface)
            .storeResultsTo(device_paths);
        for (const sdbus::ObjectPath &p : device_paths)
            register_device(state, p);

        state.manager->uponSignal("DeviceAdded")
            .onInterface(kManagerIface)
            .call([&state](const sdbus::ObjectPath &path) {
                register_device(state, path);
            });
        state.manager->uponSignal("DeviceRemoved")
            .onInterface(kManagerIface)
            .call([&state](const sdbus::ObjectPath &path) {
                auto it = std::find_if(
                    state.devices.begin(), state.devices.end(),
                    [&](const auto &e) { return e->path == path; });
                if (it != state.devices.end()) {
                    state.devices.erase(it);
                    state.dirty = true;
                }
            });
        state.manager->uponSignal("PropertiesChanged")
            .onInterface(kPropertiesIface)
            .call([&state](const std::string &,
                           const std::map<std::string, sdbus::Variant> &,
                           const std::vector<std::string> &) {
                refresh_on_battery(state);
            });

        refresh(state);
        refresh_on_battery(state);
        state.dirty = false;
        klog(
            "upower: connected, display device at %s, %zu device(s) enumerated",
            device_path.c_str(), state.devices.size());
        return true;
    } catch (const sdbus::Error &e) {
        klog("upower: connection failed (%s): %s - no battery info available",
             e.getName().c_str(), e.getMessage().c_str());
        state.device.reset();
        state.manager.reset();
        state.bus.reset();
        return false;
    }
}
