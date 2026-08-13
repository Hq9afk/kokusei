#include "launcher.h"

#include "../core/log.h"
#include "../render/icon.h"
#include "../render/icons.h"
#include "../render/image.h"
#include "../render/node.h"
#include "../render/palette.h"
#include "../render/text_field.h"
#include "../wayland/layer_surface.h"

#include <GLES2/gl2.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <unordered_set>

// -- apps_provider --

std::string to_lower(const std::string &s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return out;
}

float score_app(const std::string &name, const std::string &query) {
    std::string n = to_lower(name);
    std::string q = to_lower(query);
    if (n.empty() || q.empty())
        return -1.0f;
    size_t idx = n.find(q);
    if (idx == std::string::npos)
        return -1.0f;
    float score = (idx == 0) ? 1000.0f : 500.0f;
    score -= static_cast<float>(std::min(idx, size_t{200}));
    score -= static_cast<float>(std::min(n.size(), size_t{200})) / 10.0f;
    return score;
}

std::vector<ScoredApp> search_apps(const std::vector<DesktopEntry> &entries,
                                   const std::string &query) {
    std::vector<ScoredApp> results;
    if (query.empty())
        return results;
    for (const DesktopEntry &e : entries) {
        float s = score_app(e.name, query);
        if (s >= 0.0f)
            results.push_back({&e, s});
    }
    return results;
}

// -- desktop_entry --

namespace desktop_entry_detail {

std::optional<DesktopEntry> parse_stream(std::istream &in,
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

} // namespace desktop_entry_detail

std::optional<DesktopEntry> parse_desktop_entry(const std::string &path,
                                                const std::string &id) {
    std::ifstream f(path);
    if (!f)
        return std::nullopt;
    return desktop_entry_detail::parse_stream(f, id);
}

std::string strip_exec_field_codes(const std::string &exec) {
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

std::vector<std::string> desktop_entry_search_dirs() {
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

std::vector<DesktopEntry> scan_desktop_entries() {
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

void desktop_entry_launch(const DesktopEntry &entry) {
    std::string cmd = strip_exec_field_codes(entry.exec);
    if (entry.terminal)
        cmd = "kitty " + cmd;
    spawn_detached(cmd);
}

// -- files_provider --

std::string basename_of(const std::string &path) {
    std::string p = path;
    while (p.size() > 1 && p.back() == '/')
        p.pop_back();
    if (p == "/")
        return "/";
    size_t slash = p.find_last_of('/');
    return slash == std::string::npos ? p : p.substr(slash + 1);
}

std::string dirname_of(const std::string &path) {
    std::string p = path;
    while (p.size() > 1 && p.back() == '/')
        p.pop_back();
    if (p == "/")
        return "/";
    size_t slash = p.find_last_of('/');
    if (slash == std::string::npos)
        return "";
    return slash == 0 ? "/" : p.substr(0, slash);
}

std::string to_glob_pattern(const std::string &query) {
    std::string q = query;

    size_t b = q.find_first_not_of(' ');
    size_t e = q.find_last_not_of(' ');
    q = (b == std::string::npos) ? "" : q.substr(b, e - b + 1);
    if (q.empty())
        return "";
    if (q.starts_with("**/") || q.starts_with("/"))
        return q;
    if (q.find('/') != std::string::npos)
        return "**/" + q;
    return "**/*" + q + "*";
}

std::vector<std::string> split_query_parts(const std::string &query) {
    std::string q = to_lower(query);
    size_t b = q.find_first_not_of(' ');
    size_t e = q.find_last_not_of(' ');
    q = (b == std::string::npos) ? "" : q.substr(b, e - b + 1);
    if (q.empty())
        return {};
    if (q.find('*') == std::string::npos)
        return {q};

    std::vector<std::string> parts;
    std::stringstream ss(q);
    std::string part;
    while (std::getline(ss, part, '*')) {
        size_t pb = part.find_first_not_of(' ');
        size_t pe = part.find_last_not_of(' ');
        if (pb == std::string::npos)
            continue;
        parts.push_back(part.substr(pb, pe - pb + 1));
    }
    return parts;
}

float score_path(const std::string &name, const std::string &query) {
    std::string n = to_lower(name);
    std::vector<std::string> parts = split_query_parts(query);
    if (n.empty() || parts.empty())
        return -1.0f;

    float score = 0.0f;
    size_t cursor = 0;
    for (size_t i = 0; i < parts.size(); ++i) {
        size_t idx = n.find(parts[i], cursor);
        if (idx == std::string::npos)
            return -1.0f;
        if (i == 0) {
            score += (idx == 0) ? 1000.0f : 500.0f;
            score -= static_cast<float>(std::min(idx, size_t{200}));
        } else {
            size_t end_distance = n.size() - (idx + parts[i].size());
            score += 200.0f;
            score -= static_cast<float>(std::min(end_distance, size_t{200}));
        }
        cursor = idx + parts[i].size();
    }
    score -= static_cast<float>(std::min(n.size(), size_t{200})) / 10.0f;
    return score;
}

namespace {

std::vector<std::string> parse_lines(const std::string &raw) {
    std::vector<std::string> lines;
    std::stringstream ss(raw);
    std::string line;
    while (std::getline(ss, line)) {
        size_t b = line.find_first_not_of(" \t");
        size_t e = line.find_last_not_of(" \t");
        if (b == std::string::npos)
            continue;
        lines.push_back(line.substr(b, e - b + 1));
    }
    return lines;
}

std::string run_command(const std::vector<std::string> &argv) {
    std::string cmd;
    for (const auto &a : argv) {
        cmd += '\'';
        for (char c : a) {
            if (c == '\'')
                cmd += "'\\''";
            else
                cmd += c;
        }
        cmd += "' ";
    }
    FILE *pipe = popen(cmd.c_str(), "r");
    if (!pipe)
        return {};
    std::string out;
    std::array<char, 4096> buf{};
    size_t n;
    while ((n = fread(buf.data(), 1, buf.size(), pipe)) > 0)
        out.append(buf.data(), n);
    pclose(pipe);
    return out;
}

} // namespace

std::vector<std::string> fd_search_argv(const std::string &pattern,
                                        const std::string &search_root,
                                        bool is_dir, int max_results, int depth,
                                        bool full_path) {

    std::vector<std::string> argv = {"fd", "--glob", "--ignore-case"};
    if (full_path)
        argv.push_back("--full-path");
    argv.insert(argv.end(),
                {"--type", is_dir ? "d" : "f", "--hidden", "--no-ignore",
                 "--absolute-path", "--color", "never", "--max-results",
                 std::to_string(max_results)});
    if (depth > 0) {
        argv.push_back("--max-depth");
        argv.push_back(std::to_string(depth));
    }
    argv.push_back("--");
    argv.push_back(pattern.empty() ? "*" : pattern);
    argv.push_back(search_root);
    return argv;
}

std::vector<FileEntry> fd_search_parse_output(const std::string &raw,
                                              bool is_dir) {
    std::vector<FileEntry> results;
    for (const std::string &path : parse_lines(raw)) {
        FileEntry fe;
        fe.path = path;
        fe.name = basename_of(path);
        fe.is_dir = is_dir;
        results.push_back(std::move(fe));
    }
    return results;
}

std::vector<FileEntry> run_fd_search(const std::string &pattern,
                                     const std::string &search_root,
                                     bool is_dir, int max_results, int depth,
                                     bool full_path) {
    std::string raw = run_command(fd_search_argv(
        pattern, search_root, is_dir, max_results, depth, full_path));
    return fd_search_parse_output(raw, is_dir);
}

// -- icon_theme --

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

// -- launch_action --

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

std::string resolve_web_target(const std::string &text, const std::string &base) {
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

// -- search --

namespace {

struct PrefixEntry {
    const char *prefix;
    LauncherMode mode;
};

constexpr std::array<PrefixEntry, 5> kPrefixes = {{
    {">", LauncherMode::Run},
    {"gg", LauncherMode::Google},
    {"ddg", LauncherMode::DuckDuckGo},
    {"yt", LauncherMode::YouTube},
    {"url", LauncherMode::Url},
}};

bool is_alnum_prefix(const std::string &s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) {
        return std::isalnum(c);
    });
}

std::string trim_left(const std::string &s) {
    size_t i = 0;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])))
        ++i;
    return s.substr(i);
}

} // namespace

ModeQuery detect_mode_and_query(const std::string &raw) {
    std::string trimmed = trim_left(raw);
    if (trimmed.empty())
        return {LauncherMode::Drun, ""};

    for (const auto &p : kPrefixes) {
        std::string prefix = p.prefix;
        if (trimmed.size() < prefix.size() ||
            trimmed.compare(0, prefix.size(), prefix) != 0)
            continue;
        if (is_alnum_prefix(prefix)) {

            if (trimmed.size() > prefix.size() &&
                !std::isspace(
                    static_cast<unsigned char>(trimmed[prefix.size()])))
                continue;
        }
        std::string rest = trim_left(trimmed.substr(prefix.size()));
        return {p.mode, rest};
    }
    return {LauncherMode::Drun, trimmed};
}

std::vector<DrunResult>
combined_drun_results(const std::vector<ScoredApp> &apps,
                      const std::vector<FileEntry> &files,
                      const VisitStore &visits, int max_results) {
    struct Decorated {
        DrunResult result;
        int tier;
        int visits;
        float score;
        std::string name_lower;
    };

    std::vector<Decorated> decorated;
    decorated.reserve(apps.size() + files.size());

    for (const ScoredApp &a : apps) {
        Decorated d;
        d.result.kind = DrunResult::Kind::App;
        d.result.app = a.entry;
        d.tier = 0;
        d.visits = visit_store_get(visits, visit_store_app_key(a.entry->id));
        d.score = a.score;
        d.name_lower = to_lower(a.entry->name);
        decorated.push_back(std::move(d));
    }
    for (const FileEntry &f : files) {
        Decorated d;
        d.result.kind =
            f.is_dir ? DrunResult::Kind::Dir : DrunResult::Kind::File;
        d.result.file = f;
        d.tier = f.is_dir ? 1 : 2;
        d.visits = visit_store_get(visits, visit_store_file_key(f.path));
        d.score = f.score;
        d.name_lower = to_lower(f.name);
        decorated.push_back(std::move(d));
    }

    std::stable_sort(decorated.begin(), decorated.end(),
                     [](const Decorated &a, const Decorated &b) {
                         if (a.tier != b.tier)
                             return a.tier < b.tier;
                         if (a.visits != b.visits)
                             return a.visits > b.visits;
                         if (a.score != b.score)
                             return a.score > b.score;
                         return a.name_lower < b.name_lower;
                     });

    std::vector<DrunResult> out;
    for (size_t i = 0;
         i < decorated.size() && static_cast<int>(i) < max_results; ++i)
        out.push_back(decorated[i].result);
    return out;
}

// -- spawn --

void spawn_detached(const std::string &shell_command) {
    pid_t mid = fork();
    if (mid < 0) {
        klog("spawn: fork failed for '%s'", shell_command.c_str());
        return;
    }
    if (mid == 0) {
        pid_t grandchild = fork();
        if (grandchild == 0) {
            setsid();
            execl("/bin/sh", "sh", "-c", shell_command.c_str(), nullptr);
            _exit(127);
        }
        _exit(0);
    }
    waitpid(mid, nullptr, 0);
}

// -- submenu --

namespace {

std::string parent_of(const std::string &path) {
    std::string p = path;
    while (p.size() > 1 && p.back() == '/')
        p.pop_back();
    size_t slash = p.find_last_of('/');
    if (slash == std::string::npos)
        return "/";
    return slash == 0 ? "/" : p.substr(0, slash);
}

std::vector<SubmenuEntry> listing_to_entries(const DirLister &list_dir,
                                             const std::string &path) {
    std::vector<SubmenuEntry> out;
    for (const FileEntry &fe : list_dir(path, true)) {
        SubmenuEntry e;
        e.name = fe.name;
        e.path = fe.path;
        e.is_dir = true;
        out.push_back(e);
    }
    for (const FileEntry &fe : list_dir(path, false)) {
        SubmenuEntry e;
        e.name = fe.name;
        e.path = fe.path;
        e.is_dir = false;
        out.push_back(e);
    }
    return out;
}

} // namespace

void submenu_open_directory(SubmenuState &s, const std::string &path,
                            const DirLister &list_dir) {
    s.screen = SubmenuScreen::Browse;
    s.current_path = path;
    s.items.clear();

    SubmenuEntry open_options;
    open_options.name = "Open Directory";
    open_options.path = path;
    open_options.is_dir = true;
    open_options.icon = icon::arrow_right;
    open_options.action = SubmenuEntry::Action::OpenOptions;
    s.items.push_back(open_options);

    SubmenuEntry prev_dir;
    prev_dir.name = "Previous Directory";
    prev_dir.path = path;
    prev_dir.is_dir = true;
    prev_dir.icon = icon::arrow_left;
    prev_dir.action = SubmenuEntry::Action::PrevDir;
    s.items.push_back(prev_dir);

    for (SubmenuEntry &e : listing_to_entries(list_dir, path))
        s.items.push_back(std::move(e));
}

void submenu_open_directory_actions(SubmenuState &s, const std::string &path) {
    s.screen = SubmenuScreen::DirActions;
    s.current_path = path;
    s.items.clear();

    SubmenuEntry fm{"Open Directory in File Manager", path, true,
                    icon::arrow_right,
                    SubmenuEntry::Action::DirOpenFileManager};
    SubmenuEntry editor{"Open Directory in Editor", path, true, icon::code,
                        SubmenuEntry::Action::DirOpenEditor};
    SubmenuEntry terminal{"Open Directory in Terminal", path, true,
                          icon::terminal,
                          SubmenuEntry::Action::DirOpenTerminal};
    s.items = {fm, editor, terminal};
}

void submenu_open_file_actions(SubmenuState &s, const std::string &path) {
    bool came_from_browse = (s.screen == SubmenuScreen::Browse);
    s.screen = SubmenuScreen::FileActions;
    s.current_path = path;
    s.came_from_browse = came_from_browse;
    s.items.clear();

    SubmenuEntry open_file{"Open File", path, false, icon::arrow_right,
                           SubmenuEntry::Action::FileOpen};
    SubmenuEntry open_dir{
        "Open Containing Directory", parent_of(path), true,
        icon::folder_open, SubmenuEntry::Action::OpenContainingDir};
    s.items = {open_file, open_dir};
}

bool submenu_handle_entry(SubmenuState &s, const SubmenuEntry &entry,
                          const DirLister &list_dir) {

    const std::string path = entry.path;
    const bool is_dir = entry.is_dir;

    switch (entry.action) {
    case SubmenuEntry::Action::Back:
        submenu_close(s);
        return true;
    case SubmenuEntry::Action::OpenOptions:
    case SubmenuEntry::Action::OpenContainingDir:
        submenu_open_directory_actions(s, path);
        return true;

    case SubmenuEntry::Action::None:
        if (is_dir)
            submenu_open_directory(s, path, list_dir);
        else
            submenu_open_file_actions(s, path);
        return true;
    case SubmenuEntry::Action::PrevDir: {
        if (s.current_path == "/")
            return true;
        submenu_open_directory(s, parent_of(s.current_path), list_dir);
        return true;
    }
    default:
        return false;
    }
}

void submenu_close(SubmenuState &s) {
    s.screen = SubmenuScreen::Search;
    s.current_path.clear();
    s.came_from_browse = false;
    s.items.clear();
}

bool submenu_go_back(SubmenuState &s, const DirLister &list_dir) {
    switch (s.screen) {
    case SubmenuScreen::DirActions:
        submenu_open_directory(s, s.current_path, list_dir);
        return true;
    case SubmenuScreen::FileActions:
        if (s.came_from_browse) {
            submenu_open_directory(s, parent_of(s.current_path), list_dir);
        } else {
            submenu_close(s);
        }
        return true;
    case SubmenuScreen::Browse:
        submenu_close(s);
        return true;
    case SubmenuScreen::Search:
    default:
        return false;
    }
}

// -- visit_store --

std::string visit_store_app_key(const std::string &desktop_id) {
    return "app:" + desktop_id;
}

std::string visit_store_file_key(const std::string &file_path) {
    return "file:" + file_path;
}

std::string visit_store_default_path() {
    const char *state_home = getenv("XDG_STATE_HOME");
    std::string dir;
    if (state_home && *state_home) {
        dir = state_home;
    } else {
        const char *home = getenv("HOME");
        dir = std::string(home ? home : "") + "/.local/state";
    }
    dir += "/kokusei";
    mkdir(dir.c_str(), 0755);
    return dir + "/launcher_visits";
}

VisitStore visit_store_load(const std::string &path) {
    VisitStore vs;
    vs.path = path.empty() ? visit_store_default_path() : path;
    std::ifstream f(vs.path);
    std::string key;
    int count;
    while (f >> key >> count)
        vs.counts[key] = count;
    return vs;
}

int visit_store_get(const VisitStore &vs, const std::string &key) {
    auto it = vs.counts.find(key);
    return it == vs.counts.end() ? 0 : it->second;
}

void visit_store_record(VisitStore &vs, const std::string &key) {
    if (key.empty())
        return;
    vs.counts[key] += 1;

    std::ofstream f(vs.path, std::ios::trunc);
    if (!f) {
        klog("visit_store: failed to write '%s'", vs.path.c_str());
        return;
    }
    for (const auto &[k, c] : vs.counts)
        f << k << '\t' << c << '\n';
}

// -- launcher --

namespace {

void launcher_layer_surface_configure(void *data,
                                      zwlr_layer_surface_v1 *layer_surface,
                                      uint32_t serial, uint32_t width,
                                      uint32_t height) {
    auto *state = static_cast<LauncherState *>(data);
    zwlr_layer_surface_v1_ack_configure(layer_surface, serial);
    state->width = static_cast<int32_t>(width);
    state->height = static_cast<int32_t>(height);
    int32_t scale = state->output_scale.scale;
    if (state->egl_window)
        wl_egl_window_resize(state->egl_window, state->width * scale,
                             state->height * scale, 0, 0);
    state->configured = true;
}

void launcher_layer_surface_closed(void *, zwlr_layer_surface_v1 *) {}

constexpr zwlr_layer_surface_v1_listener launcher_layer_surface_listener = {
    .configure = launcher_layer_surface_configure,
    .closed = launcher_layer_surface_closed,
};

void launcher_update_input_region(LauncherState &state) {
    if (state.open) {
        wl_surface_set_input_region(state.surface, nullptr);
        return;
    }
    wl_region *empty_region = wl_compositor_create_region(state.compositor);
    wl_surface_set_input_region(state.surface, empty_region);
    wl_region_destroy(empty_region);
}

std::vector<FileEntry> launcher_dir_lister(const std::string &path,
                                           bool want_dirs) {
    return run_fd_search("", path, want_dirs, 50, 1, false);
}

void launcher_search_start_now(LauncherState &state) {
    state.search_query = state.effective_query;
    std::string pattern = to_glob_pattern(state.search_query);
    state.search_started_at = std::chrono::steady_clock::now();
    pid_t dirs_pid = async_process_start(
        state.search_dirs_proc,
        fd_search_argv(pattern, state.search_root, true, kLauncherMaxResults));
    pid_t files_pid = async_process_start(
        state.search_files_proc,
        fd_search_argv(pattern, state.search_root, false, kLauncherMaxResults));
    klog("launcher: search_start query='%s' dirs_pid=%d files_pid=%d",
         state.search_query.c_str(), dirs_pid, files_pid);
    state.search_running = true;
}

void launcher_search_start(LauncherState &state) {
    if (state.awaiting_restart) {
        state.pending_restart_query = state.effective_query;
        return;
    }
    if (state.search_running) {
        auto ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - state.search_started_at)
                .count();
        klog("launcher: CANCELLING still-running search after %lldms, "
             "deferring restart",
             static_cast<long long>(ms));
        state.pending_kill_dirs = async_process_cancel(state.search_dirs_proc);
        state.pending_kill_files =
            async_process_cancel(state.search_files_proc);
        state.pending_kill_since = std::chrono::steady_clock::now();
        state.pending_restart_query = state.effective_query;
        state.search_running = false;
        state.awaiting_restart = true;
        return;
    }
    launcher_search_start_now(state);
}

void launcher_query_changed(LauncherState &state) {
    ModeQuery mq = detect_mode_and_query(state.query);
    state.mode = mq.mode;
    state.effective_query = mq.query;
    state.search_dirty = true;
    state.search_dirty_at = std::chrono::steady_clock::now();
    state.cursor_blink_visible = true;
}

void launcher_query_char_push(LauncherState &state) {
    size_t idx = state.query_char_anim.size();
    state.query_char_anim.push_back({0.0f, kLauncherQuerySlideOffsetPx});
    state.animations.animate(
        0.0f, 1.0f, kLauncherQueryScaleMs, Easing::EaseOutBack,
        [&state, idx](float v) {
            if (idx < state.query_char_anim.size())
                state.query_char_anim[idx].scale = v;
        },
        {}, launcher_query_char_owner(idx, QueryCharProp::Scale));
    state.animations.animate(
        kLauncherQuerySlideOffsetPx, 0.0f, kLauncherQuerySlideMs,
        Easing::Linear,
        [&state, idx](float v) {
            if (idx < state.query_char_anim.size())
                state.query_char_anim[idx].slide_x = v;
        },
        {}, launcher_query_char_owner(idx, QueryCharProp::Slide));
}

void launcher_query_char_pop(LauncherState &state) {
    if (state.query_char_anim.empty())
        return;
    size_t idx = state.query_char_anim.size() - 1;
    state.animations.cancelForOwner(
        launcher_query_char_owner(idx, QueryCharProp::Scale));
    state.animations.cancelForOwner(
        launcher_query_char_owner(idx, QueryCharProp::Slide));
    state.query_char_anim.pop_back();
}

void launcher_query_char_clear(LauncherState &state) {
    while (!state.query_char_anim.empty())
        launcher_query_char_pop(state);
}

void launcher_launch_selected(LauncherState &state) {
    if (state.submenu.screen != SubmenuScreen::Search) {
        if (state.selected_index < 0 ||
            state.selected_index >=
                static_cast<int>(state.submenu.items.size()))
            return;
        SubmenuEntry entry = state.submenu.items[state.selected_index];
        if (submenu_handle_entry(state.submenu, entry, launcher_dir_lister)) {

            state.selected_index = 0;
            return;
        }
        launch_submenu_action(entry, state.visits);
        launcher_toggle(state, false);
        return;
    }

    if (state.mode != LauncherMode::Drun) {
        launch_non_drun(state.mode, state.effective_query);
        launcher_toggle(state, false);
        return;
    }

    if (state.selected_index < 0 ||
        state.selected_index >= static_cast<int>(state.results.size()))
        return;
    const DrunResult &r = state.results[state.selected_index];
    switch (r.kind) {
    case DrunResult::Kind::App:
        launch_drun_app(*r.app, state.visits);
        launcher_toggle(state, false);
        break;
    case DrunResult::Kind::Dir:
        submenu_open_directory(state.submenu, r.file.path, launcher_dir_lister);
        state.selected_index = 0;
        break;
    case DrunResult::Kind::File:
        submenu_open_file_actions(state.submenu, r.file.path);
        state.selected_index = 0;
        break;
    }
}

} // namespace

bool launcher_create_surface(LauncherState &state, wl_compositor *compositor,
                             zwlr_layer_shell_v1 *layer_shell,
                             wl_output *output) {
    state.compositor = compositor;
    LayerSurfaceConfig cfg{
        .layer = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
        .name_space = "kokusei-launcher",
        .anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                  ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                  ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                  ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT,
    };
    state.layer_surface =
        layer_surface_create(state.surface, compositor, layer_shell, cfg,
                             &launcher_layer_surface_listener, &state, output);
    if (!state.layer_surface)
        return false;
    state.output_scale.on_change = [&state](int32_t scale) {
        if (state.egl_window)
            wl_egl_window_resize(state.egl_window, state.width * scale,
                                 state.height * scale, 0, 0);
        if (state.frame_clock.surface)
            request_frame(state.frame_clock);
    };
    output_scale_watch(state.output_scale, state.surface);
    launcher_update_input_region(state);
    wl_surface_commit(state.surface);

    state.visits = visit_store_load();
    return true;
}

bool launcher_init_egl(LauncherState &state, Renderer &renderer,
                       EGLDisplay display, EGLConfig config,
                       EGLContext context) {
    state.renderer = &renderer;
    state.egl_display = display;
    state.egl_context = context;
    int32_t scale = state.output_scale.scale;
    state.egl_window = wl_egl_window_create(state.surface, state.width * scale,
                                            state.height * scale);
    state.egl_surface = eglCreateWindowSurface(
        display, config,
        reinterpret_cast<EGLNativeWindowType>(state.egl_window), nullptr);
    if (state.egl_surface == EGL_NO_SURFACE)
        return false;
    if (!eglMakeCurrent(display, state.egl_surface, state.egl_surface, context))
        return false;
    for (int i = 0; i < kLauncherMaxVisible; ++i)
        state.bullet_tex[i] = load_image_texture(
            KOKUSEI_BULLET_DIR "/" + std::to_string(i + 1) + ".png");
    state.frame_clock.surface = state.surface;
    state.frame_clock.draw = [&state] { launcher_paint(state); };
    return true;
}

void launcher_request_frame(LauncherState &state) {
    if (state.egl_surface == EGL_NO_SURFACE)
        return;
    request_frame(state.frame_clock);
}

// LauncherState predates OverlayPanelBase and keeps its own surface/EGL
// fields directly, so this mirrors overlay_panel_retarget's skeleton by hand
// instead of reusing the template.
void launcher_retarget(LauncherState &state, wl_compositor *compositor,
                       zwlr_layer_shell_v1 *layer_shell, wl_display *display,
                       Renderer &renderer, EGLDisplay egl_display,
                       EGLConfig egl_config, EGLContext egl_context,
                       wl_output *target_output, const char *target_name) {
    wl_output *previous_output = state.bound_output;
    klog("panel: launcher retargeting from output=%p to '%s'",
         static_cast<void *>(previous_output), target_name);

    if (state.egl_surface != EGL_NO_SURFACE) {
        eglDestroySurface(egl_display, state.egl_surface);
        state.egl_surface = EGL_NO_SURFACE;
    }
    if (state.egl_window) {
        wl_egl_window_destroy(state.egl_window);
        state.egl_window = nullptr;
    }
    if (state.layer_surface) {
        zwlr_layer_surface_v1_destroy(state.layer_surface);
        state.layer_surface = nullptr;
    }
    if (state.surface) {
        wl_surface_destroy(state.surface);
        state.surface = nullptr;
    }
    state.configured = false;
    state.open = false;
    state.opacity = 0.0f;

    auto bind_to = [&](wl_output *out) -> bool {
        if (!launcher_create_surface(state, compositor, layer_shell, out))
            return false;
        while (!state.configured)
            wl_display_dispatch(display);
        return launcher_init_egl(state, renderer, egl_display, egl_config,
                                 egl_context);
    };

    if (bind_to(target_output)) {
        state.bound_output = target_output;
        return;
    }
    if (previous_output && bind_to(previous_output)) {
        state.bound_output = previous_output;
        return;
    }
    klog("panel: launcher retarget fallback also failed");
}

void launcher_search_start_pending(LauncherState &state) {
    if (!state.awaiting_restart)
        return;
    bool still_alive = async_process_is_alive(state.pending_kill_dirs) ||
                       async_process_is_alive(state.pending_kill_files);
    auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - state.pending_kill_since)
            .count();
    if (still_alive && elapsed < kLauncherKillGraceMs)
        return;
    klog("launcher: pending restart fired (confirmed_dead=%d) after %lldms "
         "wait",
         !still_alive, static_cast<long long>(elapsed));
    state.awaiting_restart = false;
    state.pending_kill_dirs = -1;
    state.pending_kill_files = -1;
    state.effective_query = state.pending_restart_query;
    launcher_search_start_now(state);
}

bool launcher_search_poll(LauncherState &state) {
    if (!state.search_running)
        return false;
    bool dirs_done = async_process_poll(state.search_dirs_proc);
    bool files_done = async_process_poll(state.search_files_proc);
    if (!dirs_done || !files_done)
        return false;

    state.search_running = false;
    std::vector<ScoredApp> apps = search_apps(state.apps, state.search_query);
    std::vector<FileEntry> files;
    for (bool want_dirs : {true, false}) {
        const std::string &raw = want_dirs ? state.search_dirs_proc.buffer
                                           : state.search_files_proc.buffer;
        for (FileEntry &fe : fd_search_parse_output(raw, want_dirs)) {
            fe.score = score_path(fe.name, state.search_query);
            if (fe.score >= 0.0f)
                files.push_back(std::move(fe));
        }
    }
    state.results =
        combined_drun_results(apps, files, state.visits, kLauncherMaxResults);
    state.selected_index = state.results.empty() ? -1 : 0;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - state.search_started_at)
                  .count();
    klog("launcher: search_poll DONE after %lldms query='%s' results=%zu",
         static_cast<long long>(ms), state.search_query.c_str(),
         state.results.size());
    return true;
}

const Texture *launcher_icon_lookup(LauncherState &state, const std::string &id,
                                    const std::string &icon_field) {
    auto it = state.app_icon_cache.find(id);
    if (it == state.app_icon_cache.end()) {
        std::string path = resolve_app_icon_path(icon_field);
        it = state.app_icon_cache
                 .emplace(id,
                          path.empty() ? Texture{} : load_image_texture(path))
                 .first;
    }
    return it->second.id ? &it->second : nullptr;
}

int launcher_poll_timeout_ms(const LauncherState &state) {
    int timeout_ms = -1;
    if (state.search_dirty) {
        auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - state.search_dirty_at)
                .count();
        timeout_ms = static_cast<int>(
            std::max<long long>(0, kLauncherSearchDebounceMs - elapsed));
    }
    if (state.awaiting_restart)
        timeout_ms = timeout_ms < 0
                         ? kLauncherKillCheckMs
                         : std::min(timeout_ms, kLauncherKillCheckMs);
    return timeout_ms;
}

bool launcher_tick(LauncherState &state) {
    if (!state.search_dirty || launcher_poll_timeout_ms(state) > 0)
        return false;
    state.search_dirty = false;
    if (state.mode == LauncherMode::Drun && !state.effective_query.empty()) {
        launcher_search_start(state);
    } else {
        state.results.clear();
        state.selected_index = -1;
    }
    return true;
}

void launcher_toggle(LauncherState &state, bool global) {
    if (!state.layer_surface || state.egl_surface == EGL_NO_SURFACE)
        return;

    if (state.open) {
        klog("launcher: CLOSE (was_search_running=%d dirs_pid=%d "
             "files_pid=%d)",
             state.search_running, async_process_pid(state.search_dirs_proc),
             async_process_pid(state.search_files_proc));
        state.search_dirty = false;
        async_process_cancel(state.search_dirs_proc);
        async_process_cancel(state.search_files_proc);
        state.search_running = false;
        state.awaiting_restart = false;
        state.pending_kill_dirs = state.pending_kill_files = -1;

        state.animations.animate(
            state.opacity, 0.0f, kOverlayFadeMs, Easing::EaseOutCubic,
            [&state](float v) { state.opacity = v; },
            [&state] {
                state.open = false;
                state.query.clear();
                launcher_query_char_clear(state);
                state.effective_query.clear();
                state.mode = LauncherMode::Drun;
                state.results.clear();
                state.selected_index = -1;
                state.anim_height_target = -1.0f;
                state.highlight_offset_target = -1.0f;
                state.scroll_offset_target = -1.0f;
                submenu_close(state.submenu);
                zwlr_layer_surface_v1_set_keyboard_interactivity(
                    state.layer_surface,
                    ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
                launcher_update_input_region(state);
                wl_surface_commit(state.surface);
            },
            kOverlayFadeOwner);
        launcher_request_frame(state);
        return;
    }

    klog("launcher: OPEN (global=%d)", global);
    const char *home = getenv("HOME");
    state.search_root = global ? "/" : (home ? home : "/");
    state.apps = scan_desktop_entries();
    state.open = true;
    state.cursor_blink_visible = true;
    zwlr_layer_surface_v1_set_keyboard_interactivity(
        state.layer_surface,
        ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE);
    launcher_update_input_region(state);
    wl_surface_commit(state.surface);
    state.animations.animate(
        state.opacity, 1.0f, kOverlayFadeMs, Easing::EaseOutCubic,
        [&state](float v) { state.opacity = v; }, {}, kOverlayFadeOwner);
    launcher_request_frame(state);
}

void launcher_handle_key_event(LauncherState &state, const KeyEvent &event) {
    switch (event.kind) {
    case KeyKind::Text:

        if (state.submenu.screen != SubmenuScreen::Search)
            submenu_close(state.submenu);
        state.query += event.text;
        launcher_query_char_push(state);
        launcher_query_changed(state);
        break;

    case KeyKind::Backspace: {
        if (state.submenu.screen != SubmenuScreen::Search)
            submenu_close(state.submenu);

        text_field_backspace(state.query);
        launcher_query_char_pop(state);
        launcher_query_changed(state);
        break;
    }

    case KeyKind::Up:
    case KeyKind::Down: {
        int count = state.submenu.screen == SubmenuScreen::Search
                        ? static_cast<int>(state.results.size())
                        : static_cast<int>(state.submenu.items.size());
        if (count == 0) {
            state.selected_index = -1;
            break;
        }
        int delta = event.kind == KeyKind::Down ? 1 : -1;
        state.selected_index =
            std::clamp(state.selected_index + delta, 0, count - 1);
        break;
    }

    case KeyKind::Escape:
        if (state.submenu.screen != SubmenuScreen::Search) {
            submenu_go_back(state.submenu, launcher_dir_lister);
            if (state.submenu.screen == SubmenuScreen::Search)
                state.selected_index = state.results.empty() ? -1 : 0;
            else
                state.selected_index = state.submenu.items.empty() ? -1 : 0;
        } else {
            launcher_toggle(state, false);
        }
        break;

    case KeyKind::Enter:
        launcher_launch_selected(state);
        break;

    case KeyKind::Tab:
    case KeyKind::Left:
    case KeyKind::Right:
        break;
    }
}

void launcher_handle_click(LauncherState &state, double px, double py) {
    const Rect &r = state.box_rect;
    bool inside = px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
    if (!inside)
        launcher_toggle(state, false);
}

namespace {

size_t utf8_char_len(unsigned char lead) {
    if ((lead & 0x80) == 0x00)
        return 1;
    if ((lead & 0xE0) == 0xC0)
        return 2;
    if ((lead & 0xF0) == 0xE0)
        return 3;
    if ((lead & 0xF8) == 0xF0)
        return 4;
    return 1;
}

std::string elide(const std::string &s) {
    if (s.size() <= launcher_detail::kMaxRowChars)
        return s;
    size_t cut = launcher_detail::kMaxRowChars - 1;
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80)
        --cut;
    return s.substr(0, cut) + "…";
}

std::string home_relative(const std::string &path) {
    const char *home = getenv("HOME");
    if (home && *home) {
        std::string prefix(home);
        if (path.compare(0, prefix.size(), prefix) == 0) {
            if (path.size() == prefix.size())
                return "~";
            if (path[prefix.size()] == '/')
                return "~" + path.substr(prefix.size());
        }
    }
    return path;
}

const char *mode_icon(LauncherMode mode) {
    switch (mode) {
    case LauncherMode::Run:
        return icon::terminal;
    case LauncherMode::Google:
        return icon::brand_google;
    case LauncherMode::YouTube:
        return icon::brand_youtube;
    case LauncherMode::DuckDuckGo:
    case LauncherMode::Url:
        return icon::link;
    case LauncherMode::Drun:
    default:
        return icon::apps;
    }
}

struct Row {
    const char *icon;
    std::string label;
    std::string subtitle;
    const Texture *icon_tex = nullptr;
};

std::vector<Row> visible_rows(LauncherState &state, int &first) {
    std::vector<Row> rows;
    if (state.submenu.screen == SubmenuScreen::Search) {
        for (const DrunResult &r : state.results) {
            switch (r.kind) {
            case DrunResult::Kind::App: {
                Row row{icon::apps, r.app->name, ""};
                row.icon_tex =
                    launcher_icon_lookup(state, r.app->id, r.app->icon);
                rows.push_back(std::move(row));
                break;
            }
            case DrunResult::Kind::Dir:
                rows.push_back({icon::folder, r.file.name,
                                home_relative(dirname_of(r.file.path))});
                break;
            case DrunResult::Kind::File:
                rows.push_back({icon::edit, r.file.name,
                                home_relative(dirname_of(r.file.path))});
                break;
            }
        }
    } else {
        for (const SubmenuEntry &e : state.submenu.items) {
            std::string subtitle = e.action == SubmenuEntry::Action::None
                                       ? home_relative(dirname_of(e.path))
                                       : "";
            const char *row_icon =
                e.icon ? e.icon : (e.is_dir ? icon::folder : icon::edit);
            rows.push_back({row_icon, e.name, subtitle});
        }
    }

    first = 0;
    if (state.selected_index >= kLauncherMaxVisible)
        first = state.selected_index - kLauncherMaxVisible + 1;
    return rows;
}

const Texture *cached_text(TextureCache &cache, const std::string &s,
                           int32_t scale) {
    if (s.empty())
        return nullptr;
    return cache.get("t" + std::to_string(scale) + ":" + s,
                     [&] { return rasterize_text(s, scale); });
}

const Texture *cached_text_small(TextureCache &cache, const std::string &s,
                                 int32_t scale) {
    if (s.empty())
        return nullptr;
    return cache.get("s" + std::to_string(scale) + ":" + s,
                     [&] { return rasterize_text_small(s, scale); });
}

const Texture *cached_icon(TextureCache &cache, const char *codepoint,
                           int32_t scale) {
    return cache.get("i" + std::to_string(scale) + ":" + codepoint,
                     [&] { return rasterize_icon(codepoint, scale); });
}

constexpr float kLauncherListGap =
    kLauncherListTop - kLauncherSearchHeight - kLauncherPad;

int launcher_surface_height(int visible_rows) {
    float h = kLauncherPad * 2.0f + kLauncherSearchHeight;
    if (visible_rows > 0)
        h += kLauncherListGap + visible_rows * kLauncherRowHeight +
             (visible_rows - 1) * kLauncherRowSpacing;
    return static_cast<int>(h);
}

} // namespace

void launcher_paint(LauncherState &state) {
    if (state.egl_surface == EGL_NO_SURFACE)
        return;

    state.animations.tick(std::chrono::steady_clock::now());

    int first = 0;
    std::vector<Row> rows;
    if (state.open)
        rows = visible_rows(state, first);

    int visible_count =
        static_cast<int>(std::min<size_t>(rows.size(), kLauncherMaxVisible));
    int content_h = launcher_surface_height(visible_count);

    if (state.anim_height_target < 0.0f) {
        state.anim_height = static_cast<float>(content_h);
        state.anim_height_target = static_cast<float>(content_h);
    } else if (static_cast<float>(content_h) != state.anim_height_target) {
        state.anim_height_target = static_cast<float>(content_h);
        state.animations.animate(
            state.anim_height, state.anim_height_target, kLauncherHeightAnimMs,
            Easing::EaseInOutCubic,
            [&state](float v) { state.anim_height = v; }, {},
            kLauncherHeightOwner);
    }

    eglMakeCurrent(state.egl_display, state.egl_surface, state.egl_surface,
                   state.egl_context);
    int32_t scale = state.output_scale.scale;
    state.renderer->begin_frame(state.width, state.height, scale);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);

    state.scene.rebuild();
    Node *root = &state.scene.root;

    if (state.open) {
        const float *white = rgba(palette::text);
        const float *dim = rgba(palette::text_muted);

        float box_h = state.anim_height;
        float box_x =
            (static_cast<float>(state.width) - kLauncherSurfaceWidth) / 2.0f;
        float box_y = (static_cast<float>(state.height) - box_h) / 2.0f;
        state.box_rect = {box_x, box_y, kLauncherSurfaceWidth, box_h};

        node_add_rrect(root, box_x, box_y, kLauncherSurfaceWidth, box_h,
                       metrics::radius_md, kLauncherBorderWidth,
                       rgba(palette::overlay), rgba(palette::accent));

        float clip_inset = metrics::radius_md;
        Node *outer =
            node_add_group(root, box_x + clip_inset, box_y + clip_inset,
                           kLauncherSurfaceWidth - 2 * clip_inset,
                           box_h - 2 * clip_inset, true);
        auto orx = [&](float v) { return v - (box_x + clip_inset); };
        auto ory = [&](float v) { return v - (box_y + clip_inset); };

        constexpr float kTransparent[4] = {0, 0, 0, 0};
        float mode_box_x = box_x + kLauncherPad;
        float mode_box_w = kLauncherSearchHeight;
        node_add_rrect(outer, orx(mode_box_x), ory(box_y + kLauncherPad),
                       mode_box_w, kLauncherSearchHeight, metrics::radius_sm,
                       kLauncherBorderWidth, kTransparent,
                       rgba(palette::accent));
        const Texture *mode_tex =
            cached_icon(state.tcache, mode_icon(state.mode), scale);
        if (mode_tex) {
            node_add_texture(
                outer, orx(mode_box_x + (mode_box_w - mode_tex->width) / 2.0f),
                ory(box_y + kLauncherPad +
                    (kLauncherSearchHeight - mode_tex->height) / 2.0f),
                *mode_tex, white);
        }

        float field_box_x = mode_box_x + mode_box_w + kLauncherPad;
        float field_box_w =
            box_x + kLauncherSurfaceWidth - kLauncherPad - field_box_x;
        node_add_rrect(outer, orx(field_box_x), ory(box_y + kLauncherPad),
                       field_box_w, kLauncherSearchHeight, metrics::radius_sm,
                       kLauncherBorderWidth, kTransparent,
                       rgba(palette::accent));
        float text_x = field_box_x + kLauncherPad;
        float field_center_y =
            box_y + kLauncherPad + kLauncherSearchHeight / 2.0f;

        float cell_w = 0.0f;
        if (const Texture *ref_tex = cached_text(state.tcache, "M", scale))
            cell_w =
                static_cast<float>(ref_tex->width) /
                static_cast<float>(ref_tex->scale > 0 ? ref_tex->scale : 1);

        std::string display = elide(state.query);
        size_t char_index = 0;
        float cx = text_x;
        for (size_t i = 0; i < display.size();) {
            size_t len = std::min(
                utf8_char_len(static_cast<unsigned char>(display[i])),
                display.size() - i);
            std::string ch = display.substr(i, len);
            i += len;

            bool animated = char_index < state.query_char_anim.size();
            const QueryCharAnim *anim =
                animated ? &state.query_char_anim[char_index] : nullptr;
            float glyph_scale = anim ? anim->scale : 1.0f;
            float slide = anim ? anim->slide_x : 0.0f;

            const Texture *ch_tex = cached_text(state.tcache, ch, scale);
            if (ch_tex && glyph_scale > 0.0f) {
                float inv = 1.0f / static_cast<float>(
                                       ch_tex->scale > 0 ? ch_tex->scale : 1);
                float w = static_cast<float>(ch_tex->width) * inv * glyph_scale;
                float h =
                    static_cast<float>(ch_tex->height) * inv * glyph_scale;
                float cell_center_x = cx + cell_w / 2.0f + slide;
                node_add_texture_rect(outer, orx(cell_center_x - w / 2.0f),
                                      ory(field_center_y - h / 2.0f), w, h,
                                      *ch_tex, white);
            }
            cx += cell_w;
            ++char_index;
        }

        if (state.cursor_blink_visible) {
            constexpr float kCaretW = 2.0f;
            float caret_h = kLauncherSearchHeight - 2.0f * kLauncherPad;
            node_add_rect(outer, orx(cx), ory(field_center_y - caret_h / 2.0f),
                          kCaretW, caret_h, rgba(palette::text));
        }

        float content_x = mode_box_x + mode_box_w + kLauncherBulletGap;
        float list_top = box_y + kLauncherListTop;
        float list_h = box_y + box_h - clip_inset - list_top;
        Node *list_clip = node_add_group(
            outer, orx(mode_box_x), ory(list_top),
            kLauncherSurfaceWidth - 2 * kLauncherPad, list_h, true);

        constexpr float kRowPitch = kLauncherRowHeight + kLauncherRowSpacing;
        float row_bg_x = content_x - mode_box_x;
        float row_bg_w = box_x + kLauncherSurfaceWidth - kLauncherPad - content_x;

        if (state.selected_index >= 0) {
            float highlight_target =
                static_cast<float>(state.selected_index) * kRowPitch;
            if (state.highlight_offset_target < 0.0f) {
                state.highlight_offset = highlight_target;
                state.highlight_offset_target = highlight_target;
            } else if (highlight_target != state.highlight_offset_target) {
                state.highlight_offset_target = highlight_target;
                state.animations.animate(
                    state.highlight_offset, state.highlight_offset_target,
                    kLauncherHighlightAnimMs, Easing::EaseOutCubic,
                    [&state](float v) { state.highlight_offset = v; }, {},
                    kLauncherHighlightOwner);
            }

            float scroll_target = static_cast<float>(first) * kRowPitch;
            if (state.scroll_offset_target < 0.0f) {
                state.scroll_offset = scroll_target;
                state.scroll_offset_target = scroll_target;
            } else if (scroll_target != state.scroll_offset_target) {
                state.scroll_offset_target = scroll_target;
                state.animations.animate(
                    state.scroll_offset, state.scroll_offset_target,
                    kLauncherHighlightAnimMs, Easing::EaseOutCubic,
                    [&state](float v) { state.scroll_offset = v; }, {},
                    kLauncherScrollOwner);
            }
        } else {
            state.highlight_offset_target = -1.0f;
            state.scroll_offset_target = -1.0f;
        }

        for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
            bool is_selected = i == state.selected_index;
            float y = static_cast<float>(i) * kRowPitch - state.scroll_offset;

            Node *rowg = node_add_group(
                list_clip, 0, y, kLauncherSurfaceWidth - 2 * kLauncherPad,
                kLauncherRowHeight, true);
            auto lrx = [&](float v) { return v - mode_box_x; };
            auto lry = [&](float v) { return v - y; };

            constexpr float kRowTransparent[4] = {0, 0, 0, 0};
            node_add_rrect(rowg, row_bg_x, 0, row_bg_w, kLauncherRowHeight,
                           metrics::radius_sm, 0.0f,
                           rgba(palette::text_alpha04), kRowTransparent);

            float rowx = content_x + kLauncherPad;
            if (rows[i].icon_tex) {
                const Texture &tex = *rows[i].icon_tex;
                node_add_texture_rect(
                    rowg, lrx(rowx),
                    lry(y + (kLauncherRowHeight - kIconTargetSize) / 2.0f),
                    kIconTargetSize, kIconTargetSize, tex, white);
            } else {
                const Texture *row_icon =
                    cached_icon(state.tcache, rows[i].icon, scale);
                if (row_icon) {
                    node_add_texture(
                        rowg,
                        lrx(rowx + (kIconTargetSize - row_icon->width) / 2.0f),
                        lry(y + (kLauncherRowHeight - row_icon->height) / 2.0f),
                        *row_icon, is_selected ? white : dim);
                }
            }
            rowx += kIconTargetSize + kLauncherPad;
            const Texture *label =
                cached_text(state.tcache, elide(rows[i].label), scale);
            if (!rows[i].subtitle.empty()) {
                const Texture *subtitle = cached_text_small(
                    state.tcache, elide(rows[i].subtitle), scale);
                constexpr float kTwoLineTopPad = 5.0f;
                constexpr float kTwoLineBottomPad = 5.0f;
                if (label)
                    node_add_texture(rowg, lrx(rowx), lry(y + kTwoLineTopPad),
                                     *label, white);
                if (subtitle)
                    node_add_texture(rowg, lrx(rowx),
                                     lry(y + kLauncherRowHeight -
                                         subtitle->height - kTwoLineBottomPad),
                                     *subtitle, dim);
            } else if (label) {
                node_add_texture(
                    rowg, lrx(rowx),
                    lry(y + (kLauncherRowHeight - label->height) / 2.0f),
                    *label, white);
            }
        }

        for (int slot = 0; slot < kLauncherMaxVisible; ++slot) {
            const Texture &bullet = state.bullet_tex[slot];
            if (!bullet.id)
                continue;
            float slot_y = static_cast<float>(slot) * kRowPitch;
            node_add_texture_rect(
                list_clip, (mode_box_w - kLauncherBulletSize) / 2.0f,
                slot_y + (kLauncherRowHeight - kLauncherBulletSize) / 2.0f,
                kLauncherBulletSize, kLauncherBulletSize, bullet, white);
        }

        if (state.selected_index >= 0) {
            constexpr float kTransparent2[4] = {0, 0, 0, 0};
            node_add_rrect(list_clip, content_x - mode_box_x,
                           state.highlight_offset - state.scroll_offset,
                           box_x + kLauncherSurfaceWidth - kLauncherPad -
                               content_x,
                           kLauncherRowHeight, metrics::radius_sm,
                           kLauncherHighlightBorderWidth, kTransparent2,
                           rgba(palette::accent_alt));
        }
    }

    state.renderer->set_opacity(state.opacity);
    state.scene.draw(*state.renderer);
    state.renderer->set_opacity(1.0f);
    eglSwapBuffers(state.egl_display, state.egl_surface);

    if (state.animations.hasActive())
        launcher_request_frame(state);
}
