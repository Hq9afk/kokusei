#pragma once

#include "files_provider.h"

#include <functional>
#include <string>
#include <vector>

enum class SubmenuScreen { Search, Browse, DirActions, FileActions };

struct SubmenuEntry {
    std::string name;
    std::string path;
    bool is_dir = false;

    const char *icon = nullptr;
    enum class Action {
        None,
        Back,
        OpenOptions,
        OpenContainingDir,
        PrevDir,
        DirOpenFileManager,
        DirOpenEditor,
        DirOpenTerminal,
        FileOpen,
    } action = Action::None;
};

struct SubmenuState {
    SubmenuScreen screen = SubmenuScreen::Search;
    std::string current_path;
    bool came_from_browse = false;
    std::vector<SubmenuEntry> items;
};

using DirLister = std::function<std::vector<FileEntry>(const std::string &path,
                                                       bool want_dirs)>;

void submenu_close(SubmenuState &s);

void submenu_open_directory(SubmenuState &s, const std::string &path,
                            const DirLister &list_dir);

void submenu_open_directory_actions(SubmenuState &s, const std::string &path);

void submenu_open_file_actions(SubmenuState &s, const std::string &path);

bool submenu_handle_entry(SubmenuState &s, const SubmenuEntry &entry,
                          const DirLister &list_dir);

bool submenu_go_back(SubmenuState &s, const DirLister &list_dir);
