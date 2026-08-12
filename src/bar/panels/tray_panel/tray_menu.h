#pragma once

#include "tray_menu_state.h"

void tray_menu_paint(TrayMenuState &state, TrayState &tray);

void tray_menu_close(TrayMenuState &state);

void tray_menu_open(TrayMenuState &state, TrayState &tray, const TrayItem &item,
                    const Rect &anchor_cell, int32_t screen_width);

void tray_menu_handle_click(TrayMenuState &state, TrayState &tray, double px,
                            double py);
