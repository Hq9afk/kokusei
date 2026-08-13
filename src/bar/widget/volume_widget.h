#pragma once

#include "widget_capsule.h"

struct MonitorOutput;

namespace bar_detail {

Pill volume_pill(MonitorOutput &mon);

void volume_pill_handle_wheel(MonitorOutput &mon, double dy);

void volume_pill_peek_tick(MonitorOutput &mon);

bool volume_pill_peek_expire(MonitorOutput &mon);

}
