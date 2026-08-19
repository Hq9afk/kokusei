#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/file.h>
#include <unistd.h>

#include "app/single_instance_lock.h"

#include "core/log.h"

namespace {

std::string single_instance_lock_path() {
    const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
    if (!runtime_dir)
        runtime_dir = "/tmp";
    return std::string(runtime_dir) + "/kokusei.lock";
}

} // namespace

bool single_instance_try_acquire() {
    std::string path = single_instance_lock_path();
    int fd = open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd < 0) {
        klog("single_instance: open %s: %s - running unguarded", path.c_str(),
             strerror(errno));
        return true;
    }
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        if (errno == EWOULDBLOCK) {
            close(fd);
            return false;
        }
        klog("single_instance: flock %s: %s - running unguarded", path.c_str(),
             strerror(errno));
        close(fd);
        return true;
    }
    return true;
}
