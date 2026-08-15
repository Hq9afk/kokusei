#include "launcher/visit_store.h"

#include "core/log.h"

#include <cstdlib>
#include <fstream>
#include <sys/stat.h>

std::string visit_store_app_key(const std::string &desktop_id) {
    return "app:" + desktop_id;
}

std::string visit_store_file_key(const std::string &file_path) {
    return "file:" + file_path;
}

std::string visit_store_default_path() {
    const char *state_home = getenv("XDG_STATE_HOME");
    std::string dir;
    if (state_home && *state_home) {
        dir = state_home;
    } else {
        const char *home = getenv("HOME");
        dir = std::string(home ? home : "") + "/.local/state";
    }
    dir += "/kokusei";
    mkdir(dir.c_str(), 0755);
    return dir + "/launcher_visits";
}

VisitStore visit_store_load(const std::string &path) {
    VisitStore vs;
    vs.path = path.empty() ? visit_store_default_path() : path;
    std::ifstream f(vs.path);
    std::string key;
    int count;
    while (f >> key >> count)
        vs.counts[key] = count;
    return vs;
}

int visit_store_get(const VisitStore &vs, const std::string &key) {
    auto it = vs.counts.find(key);
    return it == vs.counts.end() ? 0 : it->second;
}

void visit_store_record(VisitStore &vs, const std::string &key) {
    if (key.empty())
        return;
    vs.counts[key] += 1;

    std::ofstream f(vs.path, std::ios::trunc);
    if (!f) {
        klog("visit_store: failed to write '%s'", vs.path.c_str());
        return;
    }
    for (const auto &[k, c] : vs.counts)
        f << k << '\t' << c << '\n';
}
