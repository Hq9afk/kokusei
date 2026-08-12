#pragma once

#include "volume_panel_state.h"

void volume_panel_paint(VolumePanelState &state, PipewireState &pw,
                        float pill_center_x, float bar_height,
                        float bar_top_margin);
