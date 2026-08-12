#include "network_service.h"

#include "../../core/log.h"

#include <algorithm>
#include <cctype>
#include <map>

namespace network_detail {

constexpr const char *kService = "org.freedesktop.NetworkManager";
constexpr const char *kObjectPath = "/org/freedesktop/NetworkManager";
constexpr const char *kIface = "org.freedesktop.NetworkManager";
constexpr const char *kPropertiesIface = "org.freedesktop.DBus.Properties";

namespace {

std::string to_lower(std::string s) {
    for (char &c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::vector<std::string> device_status_argv() {
    return {"nmcli",  "-t",    "-f", "DEVICE,TYPE,STATE,CONNECTION",
            "device", "status"};
}
std::vector<std::string> profile_argv() {
    return {"nmcli", "-t", "-f", "NAME,TYPE", "connection", "show"};
}
std::vector<std::string> quick_scan_argv() {
    return {"nmcli",  "-t",   "-f",   "SSID,SECURITY,SIGNAL,IN-USE",
            "device", "wifi", "list", "--rescan",
            "no"};
}
std::vector<std::string> scan_argv() {
    return {"nmcli",  "-t",   "-f",   "SSID,SECURITY,SIGNAL,IN-USE",
            "device", "wifi", "list", "--rescan",
            "yes"};
}
std::vector<std::string> connect_argv(const std::string &ssid,
                                      const std::string &password, bool saved) {
    if (saved)
        return {"nmcli", "-t", "connection", "up", "id", ssid};
    std::vector<std::string> argv = {"nmcli", "-t",      "device",
                                     "wifi",  "connect", ssid};
    if (!password.empty()) {
        argv.push_back("password");
        argv.push_back(password);
    }
    return argv;
}
std::vector<std::string> disconnect_argv(const std::string &ssid) {
    return {"nmcli", "connection", "down", "id", ssid};
}

std::vector<std::string> forget_argv(const std::string &ssid) {
    static const char *script = R"(
ssid="$1"
UUID=$(nmcli -t -f NAME,UUID,TYPE connection show | awk -F: -v target="$ssid" '$1 == target && $3 == "802-11-wireless" { print $2; exit }')
if [ -n "$UUID" ]; then
    nmcli connection delete uuid "$UUID" 2>/dev/null || true
fi
)";
    return {"sh", "-c", script, "--", ssid};
}
std::vector<std::string> connectivity_argv() {
    return {"nmcli", "networking", "connectivity", "check"};
}

void start_if_idle(AsyncProcess &proc, bool &running_flag,
                   const std::vector<std::string> &argv,
                   bool merge_stderr = false) {
    if (running_flag)
        return;
    running_flag = async_process_start(proc, argv, merge_stderr) > 0;
}

void schedule_rescan(NetworkState &state, int ms) {
    state.next_rescan_at =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    state.rescan_scheduled = true;
}

void set_connectivity(NetworkState &state, const NetworkNotifyFn &notify,
                      const std::string &value) {
    if (value == state.connectivity)
        return;
    state.connectivity = value;
    if (state.init_done && value == "portal" && notify) {
        notify("Captive Portal",
               "Sign in required for " + state.connected_ssid());
    }
}

void handle_connected_ssid_change(NetworkState &state,
                                  const NetworkNotifyFn &notify) {
    std::string cur = state.connected_ssid();
    if (cur == state.prev_connected_ssid)
        return;
    if (!cur.empty()) {
        start_if_idle(state.connectivity_proc, state.connectivity_running,
                      connectivity_argv());
    } else if (!state.ethernet_connected) {
        set_connectivity(state, notify, "unknown");
    }
    if (state.init_done && notify) {
        if (!cur.empty())
            notify("Connected", "Connected to " + cur);
        else if (!state.prev_connected_ssid.empty())
            notify("Disconnected",
                   "Disconnected from " + state.prev_connected_ssid);
    }
    state.prev_connected_ssid = cur;
}

} // namespace

} // namespace network_detail

bool network_init(NetworkState &state, sdbus::IConnection &bus) {
    using namespace network_detail;
    try {
        state.nm = sdbus::createProxy(bus, sdbus::ServiceName{kService},
                                      sdbus::ObjectPath{kObjectPath});

        state.nm->uponSignal("PropertiesChanged")
            .onInterface(kPropertiesIface)
            .call([&state](const std::string &iface,
                           const std::map<std::string, sdbus::Variant> &changed,
                           const std::vector<std::string> &) {
                if (iface != kIface)
                    return;
                auto wifi_it = changed.find("WirelessEnabled");
                if (wifi_it != changed.end() && state.init_done) {
                    bool enabled = wifi_it->second.get<bool>();
                    if (enabled != state.wifi_enabled) {
                        state.wifi_enabled = enabled;
                        state.dirty = true;
                        if (!enabled) {
                            async_process_cancel(state.profile_proc);
                            state.profile_running = false;
                            async_process_cancel(state.quick_scan_proc);
                            state.quick_scan_running = false;
                            async_process_cancel(state.scan_proc);
                            state.scan_running = false;
                            state.networks.clear();
                            state.scanning = false;
                        } else {
                            state.scanning = true;
                            state.wifi_debounce_pending = true;
                            state.wifi_debounce_at =
                                std::chrono::steady_clock::now();
                        }
                    }
                }

                if (changed.count("State") ||
                    changed.count("ActiveConnections")) {
                    start_if_idle(state.device_proc, state.device_running,
                                  device_status_argv());
                    start_if_idle(state.scan_proc, state.scan_running,
                                  scan_argv());
                }
                if (changed.count("Connectivity")) {
                    start_if_idle(state.connectivity_proc,
                                  state.connectivity_running,
                                  connectivity_argv());
                }
            });

        bool enabled = state.nm->getProperty("WirelessEnabled")
                           .onInterface(kIface)
                           .get<bool>();
        state.wifi_enabled = enabled;

        start_if_idle(state.device_proc, state.device_running,
                      device_status_argv());

        state.init_at =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
        klog("network: connected to NetworkManager, wifi_enabled=%d", enabled);
        return true;
    } catch (const sdbus::Error &e) {
        klog("network: connection failed (%s): %s - no network info available",
             e.getName().c_str(), e.getMessage().c_str());
        state.nm.reset();
        return false;
    }
}

void network_scan(NetworkState &state) {
    using namespace network_detail;
    if (!state.wifi_enabled)
        return;
    state.last_error.clear();
    if (state.profile_running || state.quick_scan_running ||
        state.scan_running) {
        state.scan_pending = true;
        return;
    }
    start_if_idle(state.profile_proc, state.profile_running, profile_argv());
    state.scanning = true;
}

void network_connect(NetworkState &state, const std::string &ssid,
                     const std::string &password) {
    if (state.connecting)
        return;
    state.connecting = true;
    state.connecting_to = ssid;
    state.last_error.clear();
    state.connect_ssid = ssid;
    state.connect_password = password;
    state.connect_saved = state.existing_profiles.count(ssid) > 0;

    state.connect_running =
        async_process_start(
            state.connect_proc,
            network_detail::connect_argv(ssid, password, state.connect_saved),
            true) > 0;
}

void network_disconnect(NetworkState &state, const std::string &ssid) {
    state.disconnect_ssid = ssid;
    state.disconnect_running =
        async_process_start(state.disconnect_proc,
                            network_detail::disconnect_argv(ssid)) > 0;
}

void network_forget(NetworkState &state, const std::string &ssid) {
    state.forget_ssid = ssid;
    state.forget_running =
        async_process_start(state.forget_proc,
                            network_detail::forget_argv(ssid)) > 0;
}

void network_set_wifi_enabled(NetworkState &state, bool enabled) {
    if (!state.nm)
        return;
    try {
        state.nm->setProperty("WirelessEnabled")
            .onInterface(network_detail::kIface)
            .toValue(enabled);
    } catch (const sdbus::Error &e) {
        klog("network: setWirelessEnabled failed (%s): %s", e.getName().c_str(),
             e.getMessage().c_str());
    }
}

bool network_poll_device(NetworkState &state, const NetworkNotifyFn &notify) {
    using namespace network_detail;
    if (!state.device_running)
        return false;
    if (!async_process_poll(state.device_proc))
        return false;
    state.device_running = false;
    NetworkDeviceStatus st =
        network_parse_device_status(state.device_proc.buffer);
    bool was_connected = state.ethernet_connected;
    state.wifi_available = st.wifi;
    state.ethernet_available = st.ethernet;
    state.ethernet_connected = st.ethernet_connected;
    state.ethernet_connection_name = st.ethernet_name;
    if (st.ethernet_connected != was_connected) {
        if (st.ethernet_connected) {
            start_if_idle(state.connectivity_proc, state.connectivity_running,
                          connectivity_argv());
        } else if (state.connected_ssid().empty()) {
            set_connectivity(state, notify, "unknown");
        }
        if (state.init_done && notify) {
            if (st.ethernet_connected)
                notify("Connected", "Connected via Ethernet");
            else
                notify("Disconnected", "Ethernet disconnected");
        }
    }
    return true;
}

bool network_poll_profile(NetworkState &state) {
    using namespace network_detail;
    if (!state.profile_running)
        return false;
    if (!async_process_poll(state.profile_proc))
        return false;
    state.profile_running = false;
    state.existing_profiles = network_parse_profiles(state.profile_proc.buffer);
    if (!state.wifi_enabled) {
        state.scanning = false;
        return true;
    }
    if (state.networks.empty() && !state.existing_profiles.empty()) {
        std::map<std::string, NetworkInfo> pre;
        for (const std::string &name : state.existing_profiles) {
            NetworkInfo info;
            info.ssid = name;
            info.security = "--";
            info.signal = 0;
            info.connected = false;
            info.existing = true;
            info.in_range = false;
            pre.emplace(name, std::move(info));
        }
        state.networks = std::move(pre);
    }
    start_if_idle(state.quick_scan_proc, state.quick_scan_running,
                  quick_scan_argv());
    return true;
}

bool network_poll_quick_scan(NetworkState &state) {
    using namespace network_detail;
    if (!state.quick_scan_running)
        return false;
    if (!async_process_poll(state.quick_scan_proc))
        return false;
    state.quick_scan_running = false;
    if (!state.wifi_enabled) {
        state.scanning = false;
        return true;
    }
    auto quick = network_parse_networks(state.quick_scan_proc.buffer,
                                        state.existing_profiles);
    bool any_in_range =
        std::any_of(quick.begin(), quick.end(),
                    [](const auto &kv) { return kv.second.in_range; });
    if (any_in_range)
        state.networks = std::move(quick);
    start_if_idle(state.scan_proc, state.scan_running, scan_argv());
    return true;
}

bool network_poll_scan(NetworkState &state, const NetworkNotifyFn &notify) {
    using namespace network_detail;
    if (!state.scan_running)
        return false;
    if (!async_process_poll(state.scan_proc))
        return false;
    state.scan_running = false;
    state.networks =
        network_parse_networks(state.scan_proc.buffer, state.existing_profiles);
    bool has_real =
        std::any_of(state.networks.begin(), state.networks.end(),
                    [](const auto &kv) { return kv.second.in_range; });

    int next_ms;
    if (state.scan_pending) {
        state.scan_pending = false;
        state.scanning = false;
        next_ms = 100;
    } else if (state.wifi_enabled && !has_real) {
        next_ms = 2000;
    } else {
        state.scanning = false;
        next_ms = 7000;
    }
    schedule_rescan(state, next_ms);
    handle_connected_ssid_change(state, notify);
    return true;
}

bool network_poll_connect(NetworkState &state, const NetworkNotifyFn &notify) {
    using namespace network_detail;
    if (!state.connect_running)
        return false;
    if (!async_process_poll(state.connect_proc))
        return false;
    state.connect_running = false;
    const std::string &text = state.connect_proc.buffer;
    bool ok = text.find("successfully activated") != std::string::npos ||
              text.find("Connection successfully") != std::string::npos;
    if (ok) {
        auto it = state.networks.find(state.connect_ssid);
        if (it != state.networks.end()) {
            it->second.connected = true;
            it->second.existing = true;
        }
    } else if (!network_detail::trim(text).empty()) {
        if (text.find("Secrets were required") != std::string::npos ||
            text.find("no secrets provided") != std::string::npos) {
            state.last_error = "Incorrect password";
            network_forget(state, state.connect_ssid);
        } else if (text.find("No network with SSID") != std::string::npos) {
            state.last_error = "Network not found";
        } else if (text.find("Timeout") != std::string::npos) {
            state.last_error = "Connection timed out";
        } else {
            state.last_error = "Connection failed";
        }
    }
    state.connecting = false;
    state.connecting_to.clear();
    schedule_rescan(state, 5000);
    handle_connected_ssid_change(state, notify);
    return true;
}

bool network_poll_disconnect(NetworkState &state,
                             const NetworkNotifyFn &notify) {
    using namespace network_detail;
    if (!state.disconnect_running)
        return false;
    if (!async_process_poll(state.disconnect_proc))
        return false;
    state.disconnect_running = false;
    auto it = state.networks.find(state.disconnect_ssid);
    if (it != state.networks.end())
        it->second.connected = false;
    schedule_rescan(state, 3000);
    handle_connected_ssid_change(state, notify);
    return true;
}

bool network_poll_forget(NetworkState &state) {
    using namespace network_detail;
    if (!state.forget_running)
        return false;
    if (!async_process_poll(state.forget_proc))
        return false;
    state.forget_running = false;
    auto it = state.networks.find(state.forget_ssid);
    if (it != state.networks.end())
        it->second.existing = false;
    schedule_rescan(state, 3000);
    return true;
}

bool network_poll_connectivity(NetworkState &state,
                               const NetworkNotifyFn &notify) {
    using namespace network_detail;
    if (!state.connectivity_running)
        return false;
    if (!async_process_poll(state.connectivity_proc))
        return false;
    state.connectivity_running = false;
    std::string text = to_lower(trim(state.connectivity_proc.buffer));
    set_connectivity(state, notify, text == "none" ? "unknown" : text);
    return true;
}

bool network_tick(NetworkState &state,
                  std::chrono::steady_clock::time_point now) {
    using namespace network_detail;
    bool dirty = false;

    if (!state.init_done) {
        if (now >= state.init_at) {
            state.init_done = true;
            if (state.wifi_enabled)
                network_scan(state);
            dirty = true;
        }
        return dirty;
    }

    if (state.wifi_debounce_pending && now >= state.wifi_debounce_at) {
        state.wifi_debounce_pending = false;
        network_scan(state);
        dirty = true;
    }
    if (state.rescan_scheduled && now >= state.next_rescan_at) {
        state.rescan_scheduled = false;
        network_scan(state);
        dirty = true;
    }

    bool connected =
        !state.connected_ssid().empty() || state.ethernet_connected;
    if (connected) {
        if (now >= state.next_connectivity_check_at) {
            state.next_connectivity_check_at = now + std::chrono::seconds(30);
            start_if_idle(state.connectivity_proc, state.connectivity_running,
                          connectivity_argv());
        }
    } else {
        state.next_connectivity_check_at = now + std::chrono::seconds(30);
    }

    bool scanning_state_visible = state.wifi_available && state.wifi_enabled &&
                                  network_visible_count(state) == 0;
    if (scanning_state_visible && now >= state.next_dot_at) {
        state.next_dot_at = now + std::chrono::milliseconds(400);
        state.scan_dot_step = (state.scan_dot_step + 1) % 4;
        dirty = true;
    }

    return dirty;
}
