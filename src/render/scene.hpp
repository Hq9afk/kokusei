#pragma once

#include "node.hpp"

struct Scene {
    Node root;

    bool dirty() const { return node_tree_dirty(root); }

    void draw(Renderer &renderer) {
        node_draw(root, renderer);
        node_clear_dirty(root);
    }

    void rebuild() { root.clear(); }
};

