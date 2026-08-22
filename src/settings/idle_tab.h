#pragma once

#include "modules/settings.h"

void idle_tab_paint(SettingsState &state, Node *root, int32_t scale, float x,
                    float y, float w, const Config &cfg);

bool idle_tab_handle_click(SettingsState &state, const Config &cfg,
                           const SettingsCommitFn &on_commit,
                           const PanelClickRegion &region);
