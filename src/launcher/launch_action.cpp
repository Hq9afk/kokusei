#include <cctype>
#include <cstdlib>
#include <regex>

#include "core/async_process.h"

#include "launcher/desktop_entry.h"
#include "launcher/launch_action.h"
#include "launcher/visit_store.h"

namespace launch_action_detail {

std::string shell_quote(const std::string &s) {
    std::string quoted = "'";
    for (char c : s) {
        if (c == '\'')
            quoted += "'\\''";
        else
            quoted += c;
    }
    quoted += "'";
    return quoted;
}

} // namespace launch_action_detail

namespace {

std::string url_encode(const std::string &text) {
    static const char *hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : text) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0xF];
        }
    }
    return out;
}

std::string trim(const std::string &s) {
    size_t b = s.find_first_not_of(" \t\n\r");
    if (b == std::string::npos)
        return "";
    size_t e = s.find_last_not_of(" \t\n\r");
    return s.substr(b, e - b + 1);
}

} // namespace

std::string make_search_url(const std::string &text, const std::string &base) {
    std::string t = trim(text);
    if (t.empty())
        return "";
    return base + url_encode(t);
}

std::string normalize_url(const std::string &text) {
    std::string t = trim(text);
    if (t.empty())
        return "";

    static const std::regex scheme_re(R"(^[a-zA-Z][a-zA-Z0-9+.-]*://)");
    if (std::regex_search(t, scheme_re))
        return t;

    if (t.starts_with("//"))
        return "https:" + t;

    if (t.find_first_of(" \t") != std::string::npos)
        return "";

    static const std::regex localhost_re(R"(^localhost([:/].*)?$)");
    if (std::regex_match(t, localhost_re))
        return "http://" + t;

    static const std::regex ipv4_re(R"(^\d{1,3}(?:\.\d{1,3}){3}([:/].*)?$)");
    if (std::regex_match(t, ipv4_re))
        return "http://" + t;

    static const std::regex host_tld_re(R"(^[^\s@]+\.[^\s@]+$)");
    if (std::regex_match(t, host_tld_re))
        return "http://" + t;

    static const std::regex host_port_re(R"(^[^\s/]+:\d+(?:/.*)?$)");
    if (std::regex_match(t, host_port_re))
        return "http://" + t;

    return "";
}

std::string resolve_web_target(const std::string &text,
                               const std::string &base) {
    std::string url = normalize_url(text);
    if (!url.empty())
        return url;
    return make_search_url(text, base);
}

bool launch_non_drun(LauncherMode mode, const std::string &query) {
    switch (mode) {
    case LauncherMode::Run: {
        std::string cmd = trim(query);
        if (cmd.empty())
            return false;
        const char *shell = getenv("SHELL");
        spawn_detached(std::string(shell ? shell : "/bin/sh") + " -lic " +
                       launch_action_detail::shell_quote(cmd));
        return true;
    }
    case LauncherMode::Google: {
        std::string url =
            make_search_url(query, "https://www.google.com/search?q=");
        if (url.empty())
            return false;
        spawn_detached("xdg-open " + launch_action_detail::shell_quote(url));
        return true;
    }
    case LauncherMode::DuckDuckGo: {
        std::string url = make_search_url(query, "https://duckduckgo.com/?q=");
        if (url.empty())
            return false;
        spawn_detached("xdg-open " + launch_action_detail::shell_quote(url));
        return true;
    }
    case LauncherMode::YouTube: {
        std::string url = make_search_url(
            query, "https://www.youtube.com/results?search_query=");
        if (url.empty())
            return false;
        spawn_detached("xdg-open " + launch_action_detail::shell_quote(url));
        return true;
    }
    case LauncherMode::Url: {
        std::string url = normalize_url(query);
        if (url.empty())
            return false;
        spawn_detached("xdg-open " + launch_action_detail::shell_quote(url));
        return true;
    }
    case LauncherMode::Drun:
    default:
        return false;
    }
}

void launch_submenu_action(const SubmenuEntry &entry, VisitStore &visits) {
    switch (entry.action) {
    case SubmenuEntry::Action::FileOpen:
        spawn_detached("xdg-open " +
                       launch_action_detail::shell_quote(entry.path));
        break;
    case SubmenuEntry::Action::DirOpenFileManager:
        spawn_detached("xdg-open " +
                       launch_action_detail::shell_quote(entry.path));
        break;
    case SubmenuEntry::Action::DirOpenEditor:
        spawn_detached("code " + launch_action_detail::shell_quote(entry.path));
        break;
    case SubmenuEntry::Action::DirOpenTerminal:
        spawn_detached("kitty " +
                       launch_action_detail::shell_quote(entry.path));
        break;
    case SubmenuEntry::Action::OpenContainingDir:
        spawn_detached("xdg-open " +
                       launch_action_detail::shell_quote(entry.path));
        return;
    default:
        return;
    }
    visit_store_record(visits, visit_store_file_key(entry.path));
}

void launch_drun_app(const DesktopEntry &entry, VisitStore &visits) {
    desktop_entry_launch(entry);
    visit_store_record(visits, visit_store_app_key(entry.id));
}
