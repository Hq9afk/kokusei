#include "bar/widget/starward_widget.h"
#include "modules/bar.h"

namespace bar_detail {

Pill starward_pill(MonitorOutput &mon) {
    return Pill{
        PillId::Starward, &bar_state(mon).starward_texture, "Starward",
        nullptr, [&mon] {
            close_other_overlays(mon, PillId::Starward);
            if (Module *starward = find_overlay_by_name(*mon.app, "starward"))
                starward->toggle_from_widget(*mon.app);
        }};
}

} // namespace bar_detail
