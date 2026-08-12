#pragma once

#include "settings_state.h"

void settings_paint(SettingsState &state, const Config &cfg,
                    const std::vector<std::string> &monitor_names);

void settings_handle_scroll(SettingsState &state, double dy);
