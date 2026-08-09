
#include "../../src/system/rfkill.hpp"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <string>
#include <unistd.h>

void test_rfkill() {
    using namespace rfkill_detail;

    std::string path = "/tmp/kokusei_test_rfkill_" + std::to_string(getpid());

    assert(!read_sysfs_uint(path).has_value());
    assert(!read_sysfs_string(path).has_value());

    {
        std::ofstream f(path);
        f << "1\n";
    }
    assert(read_sysfs_uint(path) == 1u);
    unlink(path.c_str());

    {
        std::ofstream f(path);
        f << "bluetooth\n";
    }
    assert(read_sysfs_string(path) == "bluetooth");
    unlink(path.c_str());
}

