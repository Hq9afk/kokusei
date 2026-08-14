
#include "launcher/launcher.h"
#include "core/async_process.h"

#include <cassert>
#include <dirent.h>
#include <unistd.h>

static int count_open_fds() {
    DIR *d = opendir("/proc/self/fd");
    if (!d)
        return -1;
    int count = 0;
    while (readdir(d) != nullptr)
        ++count;
    closedir(d);
    return count;
}

static bool wait_done(AsyncProcess &proc, int max_iters = 300) {
    bool done = false;
    for (int i = 0; i < max_iters && !done; ++i) {
        done = async_process_poll(proc);
        if (!done)
            usleep(5000);
    }
    return done;
}

void test_async_process() {

    assert(!async_process_is_alive(-1));
    assert(!async_process_is_alive(0));

    assert(async_process_detail_resolve_path("/bin/true") == "/bin/true");
    std::string resolved_echo = async_process_detail_resolve_path("echo");
    assert(resolved_echo.front() == '/');
    assert(access(resolved_echo.c_str(), X_OK) == 0);

    AsyncProcess proc;
    pid_t echo_pid = async_process_start(proc, {"echo", "hello"});
    assert(echo_pid > 0);
    assert(wait_done(proc));
    assert(proc.buffer == "hello\n");
    assert(proc.pid == -1);

    AsyncProcess sleeper;
    pid_t sleeper_pid = async_process_start(sleeper, {"sleep", "5"});
    assert(sleeper_pid > 0);
    assert(async_process_is_alive(sleeper_pid));
    pid_t killed = async_process_cancel(sleeper);
    assert(killed == sleeper_pid);
    assert(sleeper.pid == -1);
    assert(sleeper.wake_fd == -1);
    bool died = false;
    for (int i = 0; i < 100 && !died; ++i) {
        if (!async_process_is_alive(sleeper_pid))
            died = true;
        else
            usleep(10000);
    }
    assert(died);

    AsyncProcess idle;
    assert(async_process_cancel(idle) == -1);

    {
        AsyncProcess proc;
        for (int i = 0; i < 10; ++i) {
            pid_t slow_pid = async_process_start(proc, {"sleep", "1"});
            assert(slow_pid > 0);
            async_process_cancel(proc);
            pid_t fast_pid = async_process_start(proc, {"echo", "fresh"});
            assert(fast_pid > 0);
            assert(wait_done(proc));
            assert(proc.buffer == "fresh\n");
        }
    }

    {
        AsyncProcess proc;
        assert(async_process_start(proc, {"echo", "warmup"}) > 0);
        assert(wait_done(proc));

        int fds_before = count_open_fds();
        assert(fds_before >= 0);
        for (int i = 0; i < 30; ++i) {
            assert(async_process_start(proc, {"echo", "x"}) > 0);
            assert(wait_done(proc));
        }
        int fds_after = count_open_fds();
        assert(fds_after - fds_before < 5);
    }
}
