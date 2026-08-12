#pragma once

#include "tray_menu.h"
#include "tray_panel_state.h"

void tray_panel_paint(TrayPanelState &state, TrayState &tray,
                      float pill_center_x, float bar_height,
                      float bar_top_margin);

void tray_panel_handle_click(TrayPanelState &state, TrayState &tray,
                             TrayMenuState &menu, double px, double py,
                             uint32_t button);
