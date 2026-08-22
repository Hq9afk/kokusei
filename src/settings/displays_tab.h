#pragma once

#include "modules/settings.h"

// Shared with idle_tab.cpp: draws a "Default" + one-tile-per-monitor
// selector row and registers its MonitorSelect click regions, highlighting
// whichever name matches selected_monitor.
void settings_draw_monitor_row(SettingsState &state, Node *parent,
                               int32_t scale, float x, float y, float row_w,
                               const std::string &selected_monitor);

void displays_tab_paint(SettingsState &state, Node *root, int32_t scale,
                        float x, float y, float w, const Config &cfg);

bool displays_tab_handle_click(SettingsState &state, const Config &cfg,
                               const SettingsCommitFn &on_commit,
                               const PanelClickRegion &region);
