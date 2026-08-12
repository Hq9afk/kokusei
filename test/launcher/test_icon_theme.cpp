
#include "../../src/launcher/icon_theme.h"

#include <cassert>

void test_icon_theme() {
    assert(icon_direct_path("/home/user/icon.png") == "/home/user/icon.png");
    assert(icon_direct_path("/home/user/icon.svg") == "");
    assert(icon_direct_path("firefox") == "");
    assert(icon_direct_path("") == "");
}
