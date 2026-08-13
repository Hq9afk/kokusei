#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

constexpr float kLauncherPad = 12.0f;
constexpr float kLauncherBorderWidth = 2.0f;
constexpr float kLauncherHighlightBorderWidth = 2.0f;
constexpr float kLauncherBulletSize = 25.0f;
constexpr float kLauncherBulletGap = kLauncherPad;
constexpr float kLauncherSearchHeight = 44.0f;
constexpr float kLauncherRowHeight = 44.0f;
constexpr float kLauncherRowSpacing = 10.0f;
constexpr float kLauncherListTop = 68.0f;
constexpr int kLauncherSurfaceWidth = 700;
constexpr int kLauncherMaxVisible = 6;
constexpr int kLauncherSearchDebounceMs = 120;
constexpr int kLauncherKillGraceMs = 50;
constexpr int kLauncherKillCheckMs = 5;
constexpr int kLauncherMaxResults = 20;
constexpr float kLauncherHeightAnimMs = 200.0f;
constexpr float kLauncherHighlightAnimMs = 140.0f;
constexpr uint64_t kLauncherHeightOwner = 100;
constexpr uint64_t kLauncherHighlightOwner = 101;
constexpr uint64_t kLauncherScrollOwner = 102;

constexpr float kLauncherQueryScaleMs = 200.0f;
constexpr float kLauncherQuerySlideMs = 80.0f;
constexpr float kLauncherQuerySlideOffsetPx = 8.0f;
constexpr uint64_t kLauncherQueryCharOwnerBase = 1000;
constexpr size_t kLauncherQueryCharMax = 256;

inline constexpr int kIconTargetSize = 18;

enum class QueryCharProp : uint64_t { Scale = 0, Slide = 1 };

inline uint64_t launcher_query_char_owner(size_t index, QueryCharProp prop) {
    return kLauncherQueryCharOwnerBase + (index % kLauncherQueryCharMax) * 2 +
           static_cast<uint64_t>(prop);
}

namespace launcher_detail {
constexpr size_t kMaxRowChars = 74;
}

struct DesktopEntry {
    std::string id;
    std::string name;
    std::string exec;
    std::string icon;
    bool terminal = false;
    bool no_display = false;
    bool hidden = false;
};

struct FileEntry {
    std::string name;
    std::string path;
    bool is_dir = false;
    float score = 0.0f;
};

struct ScoredApp {
    const DesktopEntry *entry;
    float score;
};

enum class LauncherMode { Drun, Run, Google, DuckDuckGo, YouTube, Url };

struct ModeQuery {
    LauncherMode mode;
    std::string query;
};

struct DrunResult {
    enum class Kind { App, Dir, File } kind;
    const DesktopEntry *app = nullptr;
    FileEntry file;
};

struct VisitStore {
    std::unordered_map<std::string, int> counts;
    std::string path;
};

enum class SubmenuScreen { Search, Browse, DirActions, FileActions };

struct SubmenuEntry {
    std::string name;
    std::string path;
    bool is_dir = false;

    const char *icon = nullptr;
    enum class Action {
        None,
        Back,
        OpenOptions,
        OpenContainingDir,
        PrevDir,
        DirOpenFileManager,
        DirOpenEditor,
        DirOpenTerminal,
        FileOpen,
    } action = Action::None;
};

struct SubmenuState {
    SubmenuScreen screen = SubmenuScreen::Search;
    std::string current_path;
    bool came_from_browse = false;
    std::vector<SubmenuEntry> items;
};

using DirLister = std::function<std::vector<FileEntry>(const std::string &path,
                                                       bool want_dirs)>;

struct QueryCharAnim {
    float scale = 1.0f;
    float slide_x = 0.0f;
};
