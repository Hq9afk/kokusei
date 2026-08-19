#include <cassert>

#include "service/active_output.h"

void test_active_output() {
    auto *wl_a = reinterpret_cast<wl_output *>(1);
    auto *wl_b = reinterpret_cast<wl_output *>(2);
    auto *wl_c = reinterpret_cast<wl_output *>(3);
    Output a{0, wl_a, "DP-1"}, b{0, wl_b, "DP-2"}, c{0, wl_c, "HDMI-1"};
    std::vector<Output *> outputs = {&a, &b, &c};

    assert(active_output_select(outputs, "DP-2", nullptr) == wl_b);
    assert(active_output_select(outputs, "", wl_c) == wl_c);
    assert(active_output_select(outputs, "does-not-exist", wl_c) == wl_c);
    assert(active_output_select(outputs, "", nullptr) == wl_a);

    std::vector<Output *> empty;
    assert(active_output_select(empty, "", nullptr) == nullptr);
}
