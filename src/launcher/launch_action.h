#pragma once

#include "desktop_entry.h"
#include "search.h"
#include "submenu.h"
#include "visit_store.h"

#include <string>

namespace launch_action_detail {

std::string shell_quote(const std::string &s);

}

std::string make_search_url(const std::string &text, const std::string &base);

std::string normalize_url(const std::string &text);

std::string resolve_web_target(const std::string &text, const std::string &base);

bool launch_non_drun(LauncherMode mode, const std::string &query);

void launch_submenu_action(const SubmenuEntry &entry, VisitStore &visits);

void launch_drun_app(const DesktopEntry &entry, VisitStore &visits);
