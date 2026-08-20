#include <pwd.h>
#include <sys/sysinfo.h>
#include <unistd.h>

#include "app/user_info.h"

namespace user_info {

std::string username() {
    struct passwd *pw = getpwuid(getuid());
    if (!pw)
        return "unknown";
    if (pw->pw_gecos && pw->pw_gecos[0] != '\0') {
        std::string gecos = pw->pw_gecos;
        std::string name = gecos.substr(0, gecos.find(','));
        if (!name.empty())
            return name;
    }
    return pw->pw_name ? pw->pw_name : "unknown";
}

std::string uptime_string() {
    struct sysinfo info;
    if (sysinfo(&info) != 0)
        return "";
    long hours = info.uptime / 3600;
    long minutes = (info.uptime % 3600) / 60;
    auto plural = [](long n, const char *unit) {
        return std::to_string(n) + " " + unit + (n == 1 ? "" : "s");
    };
    if (hours == 0)
        return plural(minutes, "minute");
    if (minutes == 0)
        return plural(hours, "hour");
    return plural(hours, "hour") + ", " + plural(minutes, "minute");
}

} // namespace user_info
