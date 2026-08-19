#include <algorithm>

#include "service/volume_slider.h"

uint32_t volume_slider_resolve_tag_id(const PipewireState &pw,
                                      const std::string &tag) {
    if (tag == "sink")
        return pw.default_sink_id;
    if (tag == "source")
        return pw.default_source_id;
    if (tag.rfind("stream:", 0) == 0)
        return static_cast<uint32_t>(std::stoul(tag.substr(7)));
    return 0;
}

void volume_slider_apply_drag(PipewireState &pw, const DraggedSlider &drag,
                              double px) {
    float value01 =
        drag.rect.w > 0.0f
            ? std::clamp(static_cast<float>(px - drag.rect.x) / drag.rect.w,
                         0.0f, 1.0f)
            : 0.0f;
    uint32_t id = volume_slider_resolve_tag_id(pw, drag.tag);
    if (id != 0)
        pipewire_set_node_volume(pw, id, value01);
}
