#pragma once

#include "poll_source.hpp"

#include <fcntl.h>
#include <functional>
#include <mutex>
#include <unistd.h>
#include <vector>

class DeferredCall {
  public:
    static void init() {
        int fds[2];
        if (pipe(fds) != 0)
            return;
        fcntl(fds[0], F_SETFL, fcntl(fds[0], F_GETFL) | O_NONBLOCK);
        fcntl(fds[1], F_SETFL, fcntl(fds[1], F_GETFL) | O_NONBLOCK);
        read_fd() = fds[0];
        write_fd() = fds[1];
    }

    static void call_later(std::function<void()> fn) {
        {
            std::lock_guard<std::mutex> lock(mutex());
            pending().push_back(std::move(fn));
        }
        if (write_fd() >= 0) {
            char byte = 0;
            (void)!write(write_fd(), &byte, 1);
        }
    }

    static void drain() {
        char buf[64];
        while (read_fd() >= 0 && read(read_fd(), buf, sizeof(buf)) > 0) {
        }
        std::vector<std::function<void()>> fns;
        {
            std::lock_guard<std::mutex> lock(mutex());
            fns.swap(pending());
        }
        for (auto &fn : fns)
            fn();
    }

    static int poll_fd() { return read_fd(); }

  private:
    static int &read_fd() {
        static int fd = -1;
        return fd;
    }
    static int &write_fd() {
        static int fd = -1;
        return fd;
    }
    static std::mutex &mutex() {
        static std::mutex m;
        return m;
    }
    static std::vector<std::function<void()>> &pending() {
        static std::vector<std::function<void()>> v;
        return v;
    }
};

class DeferredCallPollSource : public PollSource {
  public:
    std::size_t add_poll_fds(std::vector<pollfd> &fds) override {
        if (DeferredCall::poll_fd() < 0)
            return 0;
        fds.push_back(
            {.fd = DeferredCall::poll_fd(), .events = POLLIN, .revents = 0});
        return 1;
    }

    void dispatch(const std::vector<pollfd> &, std::size_t) override {
        DeferredCall::drain();
    }
};
