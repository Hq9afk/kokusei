#pragma once

#include "bluetooth_panel_state.h"

void bluetooth_panel_paint(BluetoothPanelState &state, BluetoothState &bt,
                           float pill_center_x, float bar_height,
                           float bar_top_margin);
