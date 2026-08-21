#pragma once

#include "bar/widget/widget_capsule.h"

struct MonitorOutput;

namespace bar_detail {
Pill tray_pill(MonitorOutput &mon);
Pill cpu_pill(MonitorOutput &mon);
Pill dashboard_pill(MonitorOutput &mon);
} // namespace bar_detail
