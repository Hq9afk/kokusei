#include <algorithm>

#include "bar/panel/bluetooth_panel.h"
#include "bar/panel/network_panel.h"
#include "bar/panel/tray_panel.h"
#include "bar/panel/volume_panel.h"
#include "bar/widget/widget_capsule.h"

#include "render/icon.h"
#include "render/text.h"

#include "service/pointer.h"

Texture make_label_texture(const char *label) {
    RasterizedText r = rasterize_text(label);
    if (r.width <= 0)
        return Texture{};
    return make_texture_rgba(r.width, r.height, r.rgba.data());
}

Texture make_icon_texture(const char *codepoint_utf8) {
    RasterizedText r = rasterize_icon(codepoint_utf8);
    if (r.width <= 0)
        return Texture{};
    return make_texture_rgba(r.width, r.height, r.rgba.data());
}

namespace bar_detail {

Node *add_rrect_node(Node *parent, float x, float y, float w, float h,
                     float radius, float border_width, const float fill[4],
                     const float border[4]) {
    Node *n = parent->claim_child();
    n->kind = NodeKind::RoundedRect;
    n->x = x;
    n->y = y;
    n->w = w;
    n->h = h;
    n->radius = radius;
    n->border_width = border_width;
    n->fill = fill;
    n->border = border;
    return n;
}

Node *add_rect_node(Node *parent, float x, float y, float w, float h,
                    const float fill[4]) {
    Node *n = parent->claim_child();
    n->kind = NodeKind::Rect;
    n->x = x;
    n->y = y;
    n->w = w;
    n->h = h;
    n->fill = fill;
    return n;
}

Node *add_texture_node(Node *parent, float x, float y, const Texture &tex,
                       const float tint[4]) {
    Node *n = parent->claim_child();
    n->kind = NodeKind::Texture;
    n->x = x;
    n->y = y;
    float inv_scale = 1.0f / static_cast<float>(tex.scale > 0 ? tex.scale : 1);
    n->w = static_cast<float>(tex.width) * inv_scale;
    n->h = static_cast<float>(tex.height) * inv_scale;
    n->tex = &tex;
    n->tint = tint;
    return n;
}

float draw_static_pill_row(Node *root, float x, float height,
                           const std::vector<const Texture *> &textures,
                           const float tint[4], const float pill_bg[4]) {
    for (const Texture *tex : textures) {
        if (!tex || !tex->id)
            continue;
        float pill_w = tex->width + kPillPad * 2;
        add_rrect_node(root, x, 0, pill_w, height, metrics::radius_md,
                       metrics::border_thin, pill_bg, rgba(palette::accent));
        float ty = (height - tex->height) / 2.0f;
        add_texture_node(root, x + kPillPad, ty, *tex, tint);
        x += pill_w + kCapsuleGap;
    }
    return x;
}

size_t pill_idx(PillId id) { return static_cast<size_t>(id); }

float pill_center_x(const WidgetCapsuleState &capsule, PillId id) {
    return kPanelSideMargin + capsule.pill_expanded_center_x[pill_idx(id)];
}

PillId hit_test_pills(const WidgetCapsuleState &capsule,
                      const PointerState &pointer, wl_surface *own_surface) {
    if (pointer.focused_surface != own_surface)
        return PillId::None;
    for (size_t i = 1; i < kPillCount; ++i) {
        const Rect &r = capsule.pill_rects[i];
        if (r.w <= 0.0f)
            continue;
        if (pointer.x >= r.x && pointer.x < r.x + r.w && pointer.y >= r.y &&
            pointer.y < r.y + r.h) {
            return static_cast<PillId>(i);
        }
    }
    return PillId::None;
}

PillId panel_pill(const NetworkPanelState &network_panel,
                  const BluetoothPanelState &bluetooth_panel,
                  const VolumePanelState &volume_panel,
                  const TrayPanelState &tray_panel, bool starward_open,
                  bool controlcenter_open) {
    if (network_panel.base.open)
        return PillId::Wifi;
    if (bluetooth_panel.base.open)
        return PillId::Bluetooth;
    if (volume_panel.base.open)
        return PillId::Volume;
    if (tray_panel.base.open)
        return PillId::Tray;
    if (starward_open)
        return PillId::Starward;
    if (controlcenter_open)
        return PillId::ControlCenter;
    return PillId::None;
}

namespace {

const Texture &ensure_label_texture(WidgetCapsuleState &capsule,
                                    const Pill &p) {
    Texture &tex = capsule.pill_label_tex[pill_idx(p.id)];
    std::string &src = capsule.pill_label_src[pill_idx(p.id)];
    if (src != p.label) {
        tex = make_label_texture(p.label.c_str());
        src = p.label;
    }
    return tex;
}

} // namespace

void update_pill_expand(WidgetCapsuleState &capsule,
                        AnimationManager &animations, PillId id,
                        bool hovered_now, bool instant) {
    size_t idx = pill_idx(id);
    if (capsule.pill_expand_hovered_prev[idx] == hovered_now)
        return;
    capsule.pill_expand_hovered_prev[idx] = hovered_now;
    animations.animate(
        capsule.pill_expand_t[idx], hovered_now ? 1.0f : 0.0f,
        instant ? 0.0f : kPillExpandMs, Easing::EaseOutCubic,
        [&capsule, idx](float v) { capsule.pill_expand_t[idx] = v; }, {},
        static_cast<uint64_t>(idx));
}

float pills_row_width(WidgetCapsuleState &capsule, AnimationManager &animations,
                      const std::vector<Pill> &pills, PillId hovered,
                      float height, PillId instant_pill) {
    float w = 0;
    bool first = true;
    for (const Pill &p : pills) {
        if (!p.icon || !p.icon->id)
            continue;
        bool hovered_now = p.id == hovered && !p.label.empty();
        update_pill_expand(capsule, animations, p.id, hovered_now,
                           hovered_now && p.id == instant_pill);
        float t = capsule.pill_expand_t[pill_idx(p.id)];
        float collapsed_w = std::max(p.icon->width + kPillPad * 2, height);
        float pw = collapsed_w;
        if (t > 0.0f && !p.label.empty()) {
            const Texture &label_tex = ensure_label_texture(capsule, p);
            if (label_tex.id) {
                float expanded_w =
                    p.icon->width + kPillPad * 2 + kPillPad + label_tex.width;
                pw = collapsed_w + (expanded_w - collapsed_w) * t;
            }
        }
        if (!first)
            w += kCapsuleGap;
        w += pw;
        first = false;
    }
    return w;
}

float draw_pills(Node *root, WidgetCapsuleState &capsule,
                 AnimationManager &animations, float x, float height,
                 const std::vector<Pill> &pills, const float tint[4],
                 const float pill_bg[4], PillId hovered, PillId instant_pill) {
    for (const Pill &p : pills) {
        if (!p.icon || !p.icon->id)
            continue;

        bool hovered_now = p.id == hovered && !p.label.empty();
        update_pill_expand(capsule, animations, p.id, hovered_now,
                           hovered_now && p.id == instant_pill);
        size_t idx = pill_idx(p.id);
        float t = capsule.pill_expand_t[idx];
        const Texture *label_tex =
            !p.label.empty() ? &ensure_label_texture(capsule, p) : nullptr;
        if (label_tex && !label_tex->id)
            label_tex = nullptr;

        float collapsed_w = std::max(p.icon->width + kPillPad * 2, height);
        float expanded_w = collapsed_w;
        if (label_tex)
            expanded_w =
                p.icon->width + kPillPad * 2 + kPillPad + label_tex->width;
        float pill_w = collapsed_w + (expanded_w - collapsed_w) * t;

        Node *pill_group = node_add_group(root, x, 0, pill_w, height, true);
        add_rrect_node(pill_group, 0, 0, pill_w, height, metrics::radius_md,
                       metrics::border_thin, pill_bg,
                       p.border_color ? p.border_color : rgba(palette::accent));
        float icon_collapsed_x = (collapsed_w - p.icon->width) / 2.0f;
        float icon_expanded_x = kPillPad;
        float icon_x =
            icon_collapsed_x + (icon_expanded_x - icon_collapsed_x) * t;
        float iy = (height - p.icon->height) / 2.0f;
        add_texture_node(pill_group, icon_x, iy, *p.icon, tint);
        if (label_tex) {
            float lx = kPillPad + p.icon->width + kPillPad;
            float ly = (height - label_tex->height) / 2.0f;
            capsule.pill_label_tint[idx] = {tint[0], tint[1], tint[2], t};
            add_texture_node(pill_group, lx, ly, *label_tex,
                             rgba(capsule.pill_label_tint[idx]));
        }

        capsule.pill_rects[idx] = {x, 0.0f, pill_w, height};
        capsule.pill_expanded_center_x[idx] = x + pill_w / 2.0f;
        capsule.pill_click[idx] = p.on_click;
        x += pill_w + kCapsuleGap;
    }
    return x;
}

void dispatch_pill_click(WidgetCapsuleState &capsule,
                         const PointerState &pointer, wl_surface *own_surface) {
    PillId hit = hit_test_pills(capsule, pointer, own_surface);
    if (hit == PillId::None)
        return;
    const auto &action = capsule.pill_click[pill_idx(hit)];
    if (action)
        action();
}

} // namespace bar_detail
