#pragma once

#include "modules/settings.h"

void settings_draw_monitor_row(SettingsState &state, Node *parent,
                               int32_t scale, float x, float y, float row_w,
                               const std::string &selected_monitor);

void displays_tab_paint(SettingsState &state, Node *root, int32_t scale,
                        float x, float y, float w, const Config &cfg);

bool displays_tab_handle_click(SettingsState &state, const Config &cfg,
                               const SettingsCommitFn &on_commit,
                               const PanelClickRegion &region);
