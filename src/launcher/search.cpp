#include "search.h"

#include <algorithm>
#include <array>
#include <cctype>

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
