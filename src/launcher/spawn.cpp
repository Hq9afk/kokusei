#include "spawn.h"

#include "../core/log.h"

#include <sys/wait.h>
#include <unistd.h>

void spawn_detached(const std::string &shell_command) {
    pid_t mid = fork();
    if (mid < 0) {
        klog("spawn: fork failed for '%s'", shell_command.c_str());
        return;
    }
    if (mid == 0) {
        pid_t grandchild = fork();
        if (grandchild == 0) {
            setsid();
            execl("/bin/sh", "sh", "-c", shell_command.c_str(), nullptr);
            _exit(127);
        }
        _exit(0);
    }
    waitpid(mid, nullptr, 0);
}
