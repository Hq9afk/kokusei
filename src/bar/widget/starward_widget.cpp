#include "bar/widget/starward_widget.h"
#include "bar/bar.h"

namespace bar_detail {

Pill starward_pill(MonitorOutput &mon) {
    return Pill{
        PillId::Starward, &mon.starward_texture, "Starward", nullptr, [&mon] {
            close_other_overlays(mon, PillId::Starward);
            StarwardState &starward = mon.app->starward;
            if (!starward.base.open && mon.output.wl != starward.bound_output)
                starward_retarget(starward, mon.app->compositor,
                                  mon.app->layer_shell, mon.app->display,
                                  mon.app->renderer, mon.app->egl_display,
                                  mon.app->egl_config, mon.app->egl_context,
                                  mon.output.wl, mon.output.name.c_str());
            starward_toggle(starward, true);
        }};
}

} // namespace bar_detail
