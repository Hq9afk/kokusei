#pragma once

#include <string>
#include <vector>

#include "config/launcher_config.h"

ModeQuery detect_mode_and_query(const std::string &raw);

std::vector<DrunResult>
combined_drun_results(const std::vector<ScoredApp> &apps,
                      const std::vector<FileEntry> &files,
                      const VisitStore &visits, int max_results);
