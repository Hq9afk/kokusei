#pragma once

#include "widget_capsule.h"

struct MonitorOutput;

void init_stub_widgets(MonitorOutput &mon);

namespace bar_detail {

Pill tray_pill(MonitorOutput &mon);

Pill cpu_pill(MonitorOutput &mon);

Pill control_center_pill(MonitorOutput &mon);

}
