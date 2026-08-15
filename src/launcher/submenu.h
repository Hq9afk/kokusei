#pragma once

#include "config/launcher_config.h"

#include <string>

void submenu_close(SubmenuState &s);

void submenu_open_directory(SubmenuState &s, const std::string &path,
                            const DirLister &list_dir);

void submenu_open_directory_actions(SubmenuState &s, const std::string &path);

void submenu_open_file_actions(SubmenuState &s, const std::string &path);

bool submenu_handle_entry(SubmenuState &s, const SubmenuEntry &entry,
                          const DirLister &list_dir);

bool submenu_go_back(SubmenuState &s, const DirLister &list_dir);
