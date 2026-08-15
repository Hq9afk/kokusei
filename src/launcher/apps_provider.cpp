#include "launcher/apps_provider.h"

#include <algorithm>
#include <cctype>

std::string to_lower(const std::string &s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return out;
}

float score_app(const std::string &name, const std::string &query) {
    std::string n = to_lower(name);
    std::string q = to_lower(query);
    if (n.empty() || q.empty())
        return -1.0f;
    size_t idx = n.find(q);
    if (idx == std::string::npos)
        return -1.0f;
    float score = (idx == 0) ? 1000.0f : 500.0f;
    score -= static_cast<float>(std::min(idx, size_t{200}));
    score -= static_cast<float>(std::min(n.size(), size_t{200})) / 10.0f;
    return score;
}

std::vector<ScoredApp> search_apps(const std::vector<DesktopEntry> &entries,
                                   const std::string &query) {
    std::vector<ScoredApp> results;
    if (query.empty())
        return results;
    for (const DesktopEntry &e : entries) {
        float s = score_app(e.name, query);
        if (s >= 0.0f)
            results.push_back({&e, s});
    }
    return results;
}
