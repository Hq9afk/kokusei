#pragma once

#include "apps_provider.h"
#include "files_provider.h"
#include "visit_store.h"

#include <string>
#include <vector>

enum class LauncherMode { Drun, Run, Google, DuckDuckGo, YouTube, Url };

struct ModeQuery {
    LauncherMode mode;
    std::string query;
};

ModeQuery detect_mode_and_query(const std::string &raw);

struct DrunResult {
    enum class Kind { App, Dir, File } kind;
    const DesktopEntry *app = nullptr;
    FileEntry file;
};

std::vector<DrunResult>
combined_drun_results(const std::vector<ScoredApp> &apps,
                      const std::vector<FileEntry> &files,
                      const VisitStore &visits, int max_results);
