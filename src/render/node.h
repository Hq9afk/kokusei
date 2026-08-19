#pragma once

#include <memory>
#include <vector>

#include "render/texture.h"
#include "render/video_texture.h"

class Renderer;

enum class NodeKind { Group, Rect, RoundedRect, Texture, VideoTexture };

inline constexpr float kNodeTransparent[4] = {0, 0, 0, 0};
inline constexpr float kNodeOpaqueWhite[4] = {1, 1, 1, 1};

struct Node {
    NodeKind kind = NodeKind::Group;
    float x = 0, y = 0, w = 0, h = 0;
    float radius = 0, border_width = 0;
    const float *fill = kNodeTransparent;
    const float *border = kNodeTransparent;
    const Texture *tex = nullptr;
    const VideoTexture *video_tex = nullptr;
    const float *tint = kNodeOpaqueWhite;
    bool clip_children = false;
    bool dirty = true;
    Node *parent = nullptr;
    std::vector<std::unique_ptr<Node>> children;
    size_t live_children = 0;

    Node *claim_child();
    void clear();
};

bool node_tree_dirty(const Node &n);
void node_clear_dirty(Node &n);

Node *node_add_rect(Node *parent, float x, float y, float w, float h,
                    const float fill[4]);

Node *node_add_rrect(Node *parent, float x, float y, float w, float h,
                     float radius, float border_width, const float fill[4],
                     const float border[4]);

Node *node_add_texture_rect(Node *parent, float x, float y, float w, float h,
                            const Texture &tex, const float tint[4]);

Node *node_add_video_texture_rect(Node *parent, float x, float y, float w,
                                  float h, const VideoTexture &tex);

Node *node_add_texture(Node *parent, float x, float y, const Texture &tex,
                       const float tint[4]);

Node *node_add_group(Node *parent, float x, float y, float w, float h,
                     bool clip_children = false);

void node_draw(const Node &n, Renderer &renderer, float parent_x = 0,
               float parent_y = 0);
