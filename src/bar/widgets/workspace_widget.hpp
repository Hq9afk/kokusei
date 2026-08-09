#pragma once

#include "../../render/animation/animation.hpp"
#include "../../wayland/workspace.hpp"
#include "widget_capsule.hpp"

#include <unordered_map>
#include <vector>

struct WorkspaceWidgetState {
    std::unordered_map<int, float> width_t;
    std::unordered_map<int, bool> was_active;
};

namespace bar_detail {
constexpr float kWorkspacePillHeight = 12.0f;
constexpr float kWorkspacePillSpacing = 5.0f;
constexpr float kWorkspaceActiveWidthScale = 2.0f;
constexpr float kWorkspacePillAnimMs = 100.0f;
constexpr uint64_t kWorkspacePillOwnerBase = 200;

inline float workspace_pill_width(WorkspaceWidgetState &wstate,
                                  AnimationManager &animations, int ws_id,
                                  bool active) {
    auto it = wstate.was_active.find(ws_id);
    if (it == wstate.was_active.end()) {
        wstate.width_t[ws_id] = active ? 1.0f : 0.0f;
        wstate.was_active[ws_id] = active;
    } else if (it->second != active) {
        it->second = active;
        animations.animate(
            wstate.width_t[ws_id], active ? 1.0f : 0.0f, kWorkspacePillAnimMs,
            Easing::Linear,
            [&wstate, ws_id](float v) { wstate.width_t[ws_id] = v; }, {},
            kWorkspacePillOwnerBase + static_cast<uint64_t>(ws_id));
    }
    float t = wstate.width_t[ws_id];
    return kWorkspacePillHeight +
           kWorkspacePillHeight * (kWorkspaceActiveWidthScale - 1.0f) * t;
}

inline float workspace_row_width(WorkspaceWidgetState &wstate,
                                 AnimationManager &animations,
                                 const std::vector<Workspace> &ws_list,
                                 int active_id) {
    float w = 0;
    bool first = true;
    for (const Workspace &ws : ws_list) {
        if (!first)
            w += kWorkspacePillSpacing;
        w +=
            workspace_pill_width(wstate, animations, ws.id, ws.id == active_id);
        first = false;
    }
    return w;
}

inline float draw_workspace_row(Node *root, WorkspaceWidgetState &wstate,
                                AnimationManager &animations, float x,
                                float height,
                                const std::vector<Workspace> &ws_list,
                                int active_id, const float pill_bg[4]) {
    float ws_row_w =
        workspace_row_width(wstate, animations, ws_list, active_id);
    if (ws_row_w <= 0)
        return x;
    add_rrect_node(root, x, 0, ws_row_w + kPillPad * 2, height,
                   metrics::radius_md, metrics::border_thin, pill_bg,
                   rgba(palette::accent));
    float wx = x + kPillPad;
    float wy = (height - kWorkspacePillHeight) / 2.0f;
    for (const Workspace &ws : ws_list) {
        bool is_active = ws.id == active_id;
        float pw = workspace_pill_width(wstate, animations, ws.id, is_active);
        const float *color = is_active     ? rgba(palette::accent_alt)
                             : ws.occupied ? rgba(palette::accent)
                                           : rgba(palette::text_dim);
        add_rrect_node(root, wx, wy, pw, kWorkspacePillHeight,
                       kWorkspacePillHeight / 2.0f, 0.0f, color, color);
        wx += pw + kWorkspacePillSpacing;
    }
    return x + ws_row_w + kPillPad * 2 + kCapsuleGap;
}
}

