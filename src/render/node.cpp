#include "render/node.h"
#include "render/renderer.h"

Node *Node::claim_child() {
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
    n->video_tex = nullptr;
    n->tint = kNodeOpaqueWhite;
    n->clip_children = false;
    n->live_children = 0;
    n->dirty = true;
    ++live_children;
    return n;
}

void Node::clear() {
    live_children = 0;
    dirty = true;
}

bool node_tree_dirty(const Node &n) {
    if (n.dirty)
        return true;
    for (size_t i = 0; i < n.live_children; ++i)
        if (node_tree_dirty(*n.children[i]))
            return true;
    return false;
}

void node_clear_dirty(Node &n) {
    n.dirty = false;
    for (size_t i = 0; i < n.live_children; ++i)
        node_clear_dirty(*n.children[i]);
}

Node *node_add_rect(Node *parent, float x, float y, float w, float h,
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

Node *node_add_rrect(Node *parent, float x, float y, float w, float h,
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

Node *node_add_texture_rect(Node *parent, float x, float y, float w, float h,
                            const Texture &tex, const float tint[4]) {
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

Node *node_add_video_texture_rect(Node *parent, float x, float y, float w,
                                  float h, const VideoTexture &tex) {
    Node *n = parent->claim_child();
    n->kind = NodeKind::VideoTexture;
    n->x = x;
    n->y = y;
    n->w = w;
    n->h = h;
    n->video_tex = &tex;
    return n;
}

Node *node_add_texture(Node *parent, float x, float y, const Texture &tex,
                       const float tint[4]) {
    float inv_scale = 1.0f / static_cast<float>(tex.scale > 0 ? tex.scale : 1);
    return node_add_texture_rect(
        parent, x, y, static_cast<float>(tex.width) * inv_scale,
        static_cast<float>(tex.height) * inv_scale, tex, tint);
}

Node *node_add_group(Node *parent, float x, float y, float w, float h,
                     bool clip_children) {
    Node *n = parent->claim_child();
    n->kind = NodeKind::Group;
    n->x = x;
    n->y = y;
    n->w = w;
    n->h = h;
    n->clip_children = clip_children;
    return n;
}

void node_draw(const Node &n, Renderer &renderer, float parent_x,
               float parent_y) {
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
    case NodeKind::VideoTexture:
        if (n.video_tex && n.video_tex->y_tex)
            renderer.draw_video_texture_rect(x, y, n.w, n.h, *n.video_tex);
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
