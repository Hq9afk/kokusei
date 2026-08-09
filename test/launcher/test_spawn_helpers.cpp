
#include "../../src/launcher/spawn.hpp"

#include <cassert>
#include <cerrno>
#include <cstdlib>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

void test_spawn_helpers() {
    std::string marker = "/tmp/kokusei_test_spawn_" + std::to_string(getpid());
    spawn_detached("touch " + marker);

    bool created = false;
    for (int i = 0; i < 50 && !created; ++i) {
        if (access(marker.c_str(), F_OK) == 0)
            created = true;
        else
            usleep(20000);
    }
    assert(created);
    unlink(marker.c_str());

    errno = 0;
    pid_t reaped = waitpid(-1, nullptr, WNOHANG);
    assert(reaped == -1 && errno == ECHILD);

    for (int i = 0; i < 5; ++i)
        spawn_detached("true");
    usleep(50000);
    errno = 0;
    reaped = waitpid(-1, nullptr, WNOHANG);
    assert(reaped == -1 && errno == ECHILD);
}

