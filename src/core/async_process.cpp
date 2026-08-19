#include <sys/eventfd.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

#include "core/async_process.h"
#include "core/log.h"

std::string async_process_detail_resolve_path(const std::string &name) {
    if (name.find('/') != std::string::npos)
        return name;
    const char *path_env = getenv("PATH");
    if (!path_env)
        return name;
    std::string paths = path_env;
    size_t start = 0;
    while (start <= paths.size()) {
        size_t colon = paths.find(':', start);
        std::string dir =
            paths.substr(start, colon == std::string::npos ? std::string::npos
                                                           : colon - start);
        if (!dir.empty()) {
            std::string candidate = dir + "/" + name;
            if (access(candidate.c_str(), X_OK) == 0)
                return candidate;
        }
        if (colon == std::string::npos)
            break;
        start = colon + 1;
    }
    return name;
}

pid_t async_process_pid(const AsyncProcess &proc) {
    std::lock_guard<std::mutex> lock(proc.mutex);
    return proc.pid;
}

pid_t async_process_start(AsyncProcess &proc,
                          const std::vector<std::string> &argv,
                          bool merge_stderr) {
    int pipefd[2];
    if (pipe(pipefd) < 0) {
        klog("async_process: pipe failed");
        return -1;
    }

    std::string resolved = async_process_detail_resolve_path(argv[0]);
    std::vector<char *> cargv;
    cargv.reserve(argv.size() + 1);
    cargv.push_back(const_cast<char *>(resolved.c_str()));
    for (size_t i = 1; i < argv.size(); ++i)
        cargv.push_back(const_cast<char *>(argv[i].c_str()));
    cargv.push_back(nullptr);
    pid_t pid = fork();
    if (pid < 0) {
        klog("async_process: fork failed");
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    if (pid == 0) {
        dup2(pipefd[1], STDOUT_FILENO);
        if (merge_stderr)
            dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        execv(cargv[0], cargv.data());
        _exit(127);
    }
    close(pipefd[1]);
    int read_fd = pipefd[0];

    int wake_fd = eventfd(0, EFD_NONBLOCK);
    if (wake_fd < 0) {
        klog("async_process: eventfd failed");
        close(read_fd);
        kill(pid, SIGKILL);
        waitpid(pid, nullptr, 0);
        return -1;
    }

    uint64_t my_generation;
    int old_wake_fd;
    {
        std::lock_guard<std::mutex> lock(proc.mutex);
        proc.done = false;
        proc.buffer.clear();
        proc.pid = pid;
        old_wake_fd = proc.wake_fd;

        proc.wake_fd = wake_fd;
        my_generation = ++proc.generation;
    }
    if (old_wake_fd >= 0)
        close(old_wake_fd);

    std::thread([&proc, read_fd, pid, wake_fd, my_generation] {
        std::string local_buffer;
        char buf[4096];
        ssize_t n;
        while ((n = read(read_fd, buf, sizeof(buf))) > 0)
            local_buffer.append(buf, static_cast<size_t>(n));
        close(read_fd);
        waitpid(pid, nullptr, 0);

        std::lock_guard<std::mutex> lock(proc.mutex);
        if (proc.generation != my_generation)
            return;
        proc.buffer = std::move(local_buffer);
        proc.pid = -1;
        proc.done = true;
        uint64_t one = 1;
        (void)!write(wake_fd, &one, sizeof(one));
    }).detach();

    return pid;
}

bool async_process_poll(AsyncProcess &proc) {
    std::lock_guard<std::mutex> lock(proc.mutex);
    if (proc.wake_fd >= 0) {
        uint64_t val;
        while (read(proc.wake_fd, &val, sizeof(val)) > 0) {
        }
    }
    return proc.done;
}

pid_t async_process_cancel(AsyncProcess &proc) {
    pid_t killed = -1;
    int old_wake_fd = -1;
    {
        std::lock_guard<std::mutex> lock(proc.mutex);
        if (proc.pid > 0) {
            kill(proc.pid, SIGKILL);
            killed = proc.pid;
            proc.pid = -1;
        }
        proc.generation++;
        proc.done = false;
        proc.buffer.clear();
        old_wake_fd = proc.wake_fd;
        proc.wake_fd = -1;
    }
    if (old_wake_fd >= 0)
        close(old_wake_fd);
    return killed;
}

bool async_process_is_alive(pid_t pid) { return pid > 0 && kill(pid, 0) == 0; }

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
