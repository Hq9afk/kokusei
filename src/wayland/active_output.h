#pragma once

#include "wayland/output.h"

#include <string>
#include <vector>

// Focused-name match, then pointer_hint, then outputs.front(). Returns
// nullptr if outputs is empty.
wl_output *active_output_select(const std::vector<Output *> &outputs,
                                const std::string &focused_name,
                                wl_output *pointer_hint);
