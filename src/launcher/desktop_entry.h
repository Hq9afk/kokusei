#pragma once

#include "config/launcher_config.h"

#include <istream>
#include <optional>
#include <string>
#include <vector>

namespace desktop_entry_detail {

std::optional<DesktopEntry> parse_stream(std::istream &in,
                                         const std::string &id);

}

std::optional<DesktopEntry> parse_desktop_entry(const std::string &path,
                                                const std::string &id);

std::string strip_exec_field_codes(const std::string &exec);

std::vector<std::string> desktop_entry_search_dirs();

std::vector<DesktopEntry> scan_desktop_entries();

void desktop_entry_launch(const DesktopEntry &entry);
