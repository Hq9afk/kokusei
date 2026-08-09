#pragma once

#include "renderer.hpp"
#include "texture.hpp"
#include <memory>
#include <vector>

enum class NodeKind { Group, Rect, RoundedRect, Texture };

inline constexpr float kNodeTransparent[4] = {0, 0, 0, 0};
inline constexpr float kNodeOpaqueWhite[4] = {1, 1, 1, 1};

struct Node {
    NodeKind kind = NodeKind::Group;
    float x = 0, y = 0, w = 0, h = 0;
    float radius = 0, border_width = 0;
    const float *fill = kNodeTransparent;
    const float *border = kNodeTransparent;
    const Texture *tex = nullptr;
    const float *tint = kNodeOpaqueWhite;
    bool clip_children = false;
    bool dirty = true;
    Node *parent = nullptr;
    std::vector<std::unique_ptr<Node>> children;
    size_t live_children = 0;

    Node *claim_child() {
        Node *n;
        if (live_children < children.size()) {
            n = children[live_children].get();
        } else {
            children.push_back(std::make_unique<Node>());
            n = children.back().get();
        }
        n->parent = this;
        n->kind = NodeKind::Group;
        n->x = n->y = n->w = n->h = 0;
        n->radius = n->border_width = 0;
        n->fill = kNodeTransparent;
        n->border = kNodeTransparent;
        n->tex = nullptr;
        n->tint = kNodeOpaqueWhite;
        n->clip_children = false;
        n->live_children = 0;
        n->dirty = true;
        ++live_children;
        return n;
    }

    void mark_dirty() {
        for (Node *n = this; n; n = n->parent)
            n->dirty = true;
    }

    void clear() {
        live_children = 0;
        dirty = true;
    }
};

inline bool node_tree_dirty(const Node &n) {
    if (n.dirty)
        return true;
    for (size_t i = 0; i < n.live_children; ++i)
        if (node_tree_dirty(*n.children[i]))
            return true;
    return false;
}

inline void node_clear_dirty(Node &n) {
    n.dirty = false;
    for (size_t i = 0; i < n.live_children; ++i)
        node_clear_dirty(*n.children[i]);
}

inline Node *node_add_rect(Node *parent, float x, float y, float w, float h,
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

inline Node *node_add_rrect(Node *parent, float x, float y, float w, float h,
                            float radius, float border_width,
                            const float fill[4], const float border[4]) {
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

inline Node *node_add_texture_rect(Node *parent, float x, float y, float w,
                                   float h, const Texture &tex,
                                   const float tint[4]) {
    Node *n = parent->claim_child();
    n->kind = NodeKind::Texture;
    n->x = x;
    n->y = y;
    n->w = w;
    n->h = h;
    n->tex = &tex;
    n->tint = tint;
    return n;
}

inline Node *node_add_texture(Node *parent, float x, float y,
                              const Texture &tex, const float tint[4]) {
    float inv_scale = 1.0f / static_cast<float>(tex.scale > 0 ? tex.scale : 1);
    return node_add_texture_rect(parent, x, y,
                                 static_cast<float>(tex.width) * inv_scale,
                                 static_cast<float>(tex.height) * inv_scale,
                                 tex, tint);
}

inline Node *node_add_group(Node *parent, float x, float y, float w, float h,
                            bool clip_children = false) {
    Node *n = parent->claim_child();
    n->kind = NodeKind::Group;
    n->x = x;
    n->y = y;
    n->w = w;
    n->h = h;
    n->clip_children = clip_children;
    return n;
}

inline void node_draw(const Node &n, Renderer &renderer, float parent_x = 0,
                      float parent_y = 0) {
    float x = parent_x + n.x, y = parent_y + n.y;
    switch (n.kind) {
    case NodeKind::Rect:
        renderer.draw_rect(x, y, n.w, n.h, n.fill);
        break;
    case NodeKind::RoundedRect:
        renderer.draw_rounded_rect(x, y, n.w, n.h, n.radius, n.border_width,
                                   n.fill, n.border);
        break;
    case NodeKind::Texture:
        if (n.tex && n.tex->id)
            renderer.draw_texture_rect(x, y, n.w, n.h, *n.tex, n.tint);
        break;
    case NodeKind::Group:
        break;
    }
    if (n.clip_children) {
        ScopedClip clip(renderer, x, y, n.w, n.h);
        for (size_t i = 0; i < n.live_children; ++i)
            node_draw(*n.children[i], renderer, x, y);
    } else {
        for (size_t i = 0; i < n.live_children; ++i)
            node_draw(*n.children[i], renderer, x, y);
    }
}
