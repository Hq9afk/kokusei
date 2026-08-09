#pragma once

#include "spawn.hpp"

#include <algorithm>
#include <cstdlib>
#include <dirent.h>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

struct DesktopEntry {
    std::string id;
    std::string name;
    std::string exec;
    std::string icon;
    bool terminal = false;
    bool no_display = false;
    bool hidden = false;
};

namespace desktop_entry_detail {

inline std::optional<DesktopEntry> parse_stream(std::istream &in,
                                                const std::string &id) {
    DesktopEntry e;
    e.id = id;
    std::string line;
    bool in_section = false;
    bool have_type_app = true;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty() || line[0] == '#')
            continue;
        if (line[0] == '[') {
            in_section = (line == "[Desktop Entry]");
            continue;
        }
        if (!in_section)
            continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);
        if (key == "Name" && e.name.empty())
            e.name = value;
        else if (key == "Exec")
            e.exec = value;
        else if (key == "Icon")
            e.icon = value;
        else if (key == "Terminal")
            e.terminal = (value == "true");
        else if (key == "NoDisplay")
            e.no_display = (value == "true");
        else if (key == "Hidden")
            e.hidden = (value == "true");
        else if (key == "Type")
            have_type_app = (value == "Application");
    }
    if (!have_type_app || e.name.empty() || e.exec.empty())
        return std::nullopt;
    return e;
}

}

inline std::optional<DesktopEntry> parse_desktop_entry(const std::string &path,
                                                       const std::string &id) {
    std::ifstream f(path);
    if (!f)
        return std::nullopt;
    return desktop_entry_detail::parse_stream(f, id);
}

inline std::string strip_exec_field_codes(const std::string &exec) {
    std::string out;
    out.reserve(exec.size());
    for (size_t i = 0; i < exec.size(); ++i) {
        if (exec[i] != '%') {
            out += exec[i];
            continue;
        }
        if (i + 1 >= exec.size())
            break;
        char code = exec[++i];
        if (code == '%') {
            out += '%';
        } else {

            if (i + 1 < exec.size() && exec[i + 1] == ' ') {
                if (i + 2 >= exec.size() || exec[i + 2] != '%') {
                    ++i;
                }
            }
        }
    }
    return out;
}

inline std::vector<std::string> desktop_entry_search_dirs() {
    std::vector<std::string> dirs;
    const char *data_dirs = getenv("XDG_DATA_DIRS");
    std::string joined =
        data_dirs && *data_dirs ? data_dirs : "/usr/local/share:/usr/share";
    std::stringstream ss(joined);
    std::string part;
    while (std::getline(ss, part, ':'))
        if (!part.empty())
            dirs.push_back(part + "/applications");

    const char *home = getenv("HOME");
    if (home)
        dirs.insert(dirs.begin(),
                    std::string(home) + "/.local/share/applications");
    return dirs;
}

inline std::vector<DesktopEntry> scan_desktop_entries() {
    std::vector<DesktopEntry> result;
    std::unordered_set<std::string> seen_ids;

    for (const std::string &dir : desktop_entry_search_dirs()) {
        DIR *d = opendir(dir.c_str());
        if (!d)
            continue;
        while (dirent *ent = readdir(d)) {
            std::string name = ent->d_name;
            if (name.size() < 9 || name.substr(name.size() - 8) != ".desktop")
                continue;
            if (!seen_ids.insert(name).second)
                continue;

            auto entry = parse_desktop_entry(dir + "/" + name, name);
            if (!entry || entry->no_display || entry->hidden)
                continue;
            result.push_back(std::move(*entry));
        }
        closedir(d);
    }
    return result;
}

inline void desktop_entry_launch(const DesktopEntry &entry) {
    std::string cmd = strip_exec_field_codes(entry.exec);
    if (entry.terminal)
        cmd = "kitty " + cmd;
    spawn_detached(cmd);
}
