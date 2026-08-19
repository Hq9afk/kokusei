#pragma once

#include <array>
#include <chrono>
#include <functional>
#include <string>
#include <vector>

#include "config/bar_config.h"

#include "render/animation.h"
#include "render/node.h"
#include "render/palette.h"
#include "render/rect.h"
#include "render/texture.h"

struct PointerState;
struct wl_surface;

enum class PillId : int {
    None = 0,
    Starward,
    Tray,
    Cpu,
    Wifi,
    Bluetooth,
    Volume,
    Battery,
    ControlCenter,
    Count
};
constexpr size_t kPillCount = static_cast<size_t>(PillId::Count);

struct Pill {
    PillId id;
    const Texture *icon;
    std::string label;
    const float *border_color = nullptr;
    std::function<void()> on_click = nullptr;
};

struct WidgetCapsuleState {
    std::array<Texture, kPillCount> pill_label_tex{};
    std::array<std::string, kPillCount> pill_label_src{};
    std::array<Rect, kPillCount> pill_rects{};
    std::array<float, kPillCount> pill_expanded_center_x{};
    std::array<std::function<void()>, kPillCount> pill_click{};
    std::array<float, kPillCount> pill_expand_t{};
    std::array<bool, kPillCount> pill_expand_hovered_prev{};
    std::array<Color, kPillCount> pill_label_tint{};

    PillId panel_pill_prev = PillId::None;
    PillId label_linger_pill = PillId::None;
    std::chrono::steady_clock::time_point label_linger_until{};
};

Texture make_label_texture(const char *label);

Texture make_icon_texture(const char *codepoint_utf8);

namespace bar_detail {

Node *add_rrect_node(Node *parent, float x, float y, float w, float h,
                     float radius, float border_width, const float fill[4],
                     const float border[4]);

Node *add_rect_node(Node *parent, float x, float y, float w, float h,
                    const float fill[4]);

Node *add_texture_node(Node *parent, float x, float y, const Texture &tex,
                       const float tint[4]);

float draw_static_pill_row(Node *root, float x, float height,
                           const std::vector<const Texture *> &textures,
                           const float tint[4], const float pill_bg[4]);

size_t pill_idx(PillId id);

float pill_center_x(const WidgetCapsuleState &capsule, PillId id);

PillId hit_test_pills(const WidgetCapsuleState &capsule,
                      const PointerState &pointer, wl_surface *own_surface);

void update_pill_expand(WidgetCapsuleState &capsule,
                        AnimationManager &animations, PillId id,
                        bool hovered_now, bool instant = false);

float pills_row_width(WidgetCapsuleState &capsule, AnimationManager &animations,
                      const std::vector<Pill> &pills, PillId hovered,
                      float height, PillId instant_pill = PillId::None);

float draw_pills(Node *root, WidgetCapsuleState &capsule,
                 AnimationManager &animations, float x, float height,
                 const std::vector<Pill> &pills, const float tint[4],
                 const float pill_bg[4], PillId hovered,
                 PillId instant_pill = PillId::None);

void dispatch_pill_click(WidgetCapsuleState &capsule,
                         const PointerState &pointer, wl_surface *own_surface);

} // namespace bar_detail

struct NetworkPanelState;
struct BluetoothPanelState;
struct VolumePanelState;
struct TrayPanelState;

namespace bar_detail {

PillId panel_pill(const NetworkPanelState &network_panel,
                  const BluetoothPanelState &bluetooth_panel,
                  const VolumePanelState &volume_panel,
                  const TrayPanelState &tray_panel, bool starward_open,
                  bool controlcenter_open);

}
