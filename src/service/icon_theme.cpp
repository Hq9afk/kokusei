#include <cstdlib>
#include <filesystem>
#include <vector>

#include "service/icon_theme.h"

std::string icon_direct_path(const std::string &icon_field) {
    if (!icon_field.starts_with('/'))
        return "";
    return icon_field.ends_with(".png") ? icon_field : "";
}

namespace {

const std::vector<std::string> &apps_dirs() {
    static const std::vector<std::string> dirs = [] {
        std::vector<std::string> d;
        std::string home = getenv("HOME") ? getenv("HOME") : "";

        for (const char *size : {"22x22", "24x24", "32x32", "48x48", "64x64",
                                 "128x128", "256x256"}) {
            if (!home.empty())
                d.push_back(home + "/.local/share/icons/hicolor/" + size +
                            "/apps/");
            d.push_back(std::string("/usr/share/icons/hicolor/") + size +
                        "/apps/");
        }
        d.push_back("/usr/share/pixmaps/");
        return d;
    }();
    return dirs;
}

} // namespace

std::string resolve_app_icon_path(const std::string &icon_field) {
    if (icon_field.empty())
        return "";
    std::string direct = icon_direct_path(icon_field);
    if (!direct.empty())
        return std::filesystem::exists(direct) ? direct : "";
    if (icon_field.starts_with('/'))
        return "";

    for (const std::string &dir : apps_dirs()) {
        std::string candidate = dir + icon_field + ".png";
        if (std::filesystem::exists(candidate))
            return candidate;
    }
    return "";
}
