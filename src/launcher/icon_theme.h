#pragma once

#include <string>

std::string icon_direct_path(const std::string &icon_field);

inline constexpr int kIconTargetSize = 18;

std::string resolve_app_icon_path(const std::string &icon_field);
