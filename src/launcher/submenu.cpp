#include "launcher/submenu.h"

#include "render/icons.h"

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
    SubmenuEntry open_dir{"Open Containing Directory", parent_of(path), true,
                          icon::folder_open,
                          SubmenuEntry::Action::OpenContainingDir};
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
