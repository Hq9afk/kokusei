#include "upower.h"

#include "../core/log.h"

#include <cmath>
#include <map>
#include <string>
#include <vector>

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

} // namespace

bool upower_init(UpowerState &state) {
    try {
        state.bus = sdbus::createSystemBusConnection();
        auto manager =
            sdbus::createProxy(*state.bus, sdbus::ServiceName{kService},
                               sdbus::ObjectPath{kManagerPath});

        sdbus::ObjectPath device_path;
        manager->callMethod("GetDisplayDevice")
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

        refresh(state);
        state.dirty = false;
        klog("upower: connected, display device at %s", device_path.c_str());
        return true;
    } catch (const sdbus::Error &e) {
        klog("upower: connection failed (%s): %s - no battery info available",
             e.getName().c_str(), e.getMessage().c_str());
        state.device.reset();
        state.bus.reset();
        return false;
    }
}
