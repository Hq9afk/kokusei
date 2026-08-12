#pragma once

#include "desktop_entry.h"

#include <string>
#include <vector>

struct ScoredApp {
    const DesktopEntry *entry;
    float score;
};

std::string to_lower(const std::string &s);

float score_app(const std::string &name, const std::string &query);

std::vector<ScoredApp> search_apps(const std::vector<DesktopEntry> &entries,
                                   const std::string &query);
