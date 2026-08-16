#pragma once

#include "bar/widget/widget_capsule.h"

#include "render/animation.h"
#include "service/workspace.h"

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
float draw_workspace_row(Node *root, WorkspaceWidgetState &wstate,
                         AnimationManager &animations, float x, float height,
                         const std::vector<Workspace> &ws_list, int active_id,
                         const float pill_bg[4]);

}
