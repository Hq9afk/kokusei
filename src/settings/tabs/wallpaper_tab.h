#pragma once

#include "settings/settings.h"

float wallpaper_tab_paint(SettingsState &state, Node *root, int32_t scale,
                          float x, float y, const Config &cfg);

bool wallpaper_tab_handle_click(SettingsState &state, const Config &cfg,
                                const SettingsCommitFn &on_commit,
                                const PanelClickRegion &region);

void wallpaper_tab_handle_scroll(SettingsState &state, double dy);
