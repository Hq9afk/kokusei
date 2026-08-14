#include "service/active_output.h"

wl_output *active_output_select(const std::vector<Output *> &outputs,
                                const std::string &focused_name,
                                wl_output *pointer_hint) {
    if (!focused_name.empty()) {
        for (Output *o : outputs)
            if (o->name == focused_name)
                return o->wl;
    }
    if (pointer_hint)
        return pointer_hint;
    return outputs.empty() ? nullptr : outputs.front()->wl;
}
