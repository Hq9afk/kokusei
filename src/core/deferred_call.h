#pragma once

#include <functional>
#include <mutex>
#include <vector>

#include "core/poll_source.h"

class DeferredCall {
  public:
    static void init();
    static void call_later(std::function<void()> fn);
    static void drain();
    static int poll_fd();

  private:
    static int &read_fd();
    static int &write_fd();
    static std::mutex &mutex();
    static std::vector<std::function<void()>> &pending();
};

class DeferredCallPollSource : public PollSource {
  public:
    std::size_t add_poll_fds(std::vector<pollfd> &fds) override;
    void dispatch(const std::vector<pollfd> &, std::size_t) override;
};
