#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <sys/types.h>
#include <vector>

struct AsyncProcess {
    mutable std::mutex mutex;
    bool done = false;
    std::string buffer;
    pid_t pid = -1;
    int wake_fd = -1;
    uint64_t generation = 0;
};

std::string async_process_detail_resolve_path(const std::string &name);

pid_t async_process_pid(const AsyncProcess &proc);

pid_t async_process_start(AsyncProcess &proc,
                          const std::vector<std::string> &argv,
                          bool merge_stderr = false);

bool async_process_poll(AsyncProcess &proc);

pid_t async_process_cancel(AsyncProcess &proc);

bool async_process_is_alive(pid_t pid);
