#pragma once

#include <cstdint>
#include <functional>

struct Config;
struct Node;
struct PanelClickRegion;
struct SettingsState;

float visualizer_tab_paint(SettingsState &state, Node *root, int32_t scale,
                           float x, float y, const Config &cfg);

bool visualizer_tab_handle_click(SettingsState &state, const Config &cfg,
                                 const std::function<void(Config)> &on_commit,
                                 const PanelClickRegion &region);
