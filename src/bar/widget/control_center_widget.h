#pragma once

#include "bar/widget/widget_capsule.h"

struct MonitorOutput;

namespace bar_detail {
Pill tray_pill(MonitorOutput &mon);
Pill cpu_pill(MonitorOutput &mon);
Pill control_center_pill(MonitorOutput &mon);
}
