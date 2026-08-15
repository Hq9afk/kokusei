#pragma once

#include "service/keyboard.h"

#include <vector>

struct WaylandState;

void dispatch_key_events(WaylandState &state,
                         const std::vector<KeyEvent> &events);
