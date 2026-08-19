#pragma once

#include <vector>

#include "service/keyboard.h"

struct WaylandState;

void dispatch_key_events(WaylandState &state,
                         const std::vector<KeyEvent> &events);
