#pragma once

#include <cstddef>
#include <functional>
#include <poll.h>
#include <vector>

class PollSource {
  public:
    virtual ~PollSource() = default;

    virtual std::size_t add_poll_fds(std::vector<pollfd> &fds) = 0;

    virtual void dispatch(const std::vector<pollfd> &fds,
                          std::size_t start_idx) = 0;
};

class FnPollSource : public PollSource {
  public:
    using DispatchFn = std::function<void()>;

    FnPollSource(int fd, short events, DispatchFn fn)
        : fd_(fd), events_(events), fd2_(-1), events2_(0), fn_(std::move(fn)) {}
    FnPollSource(int fd, short events, int fd2, short events2, DispatchFn fn)
        : fd_(fd), events_(events), fd2_(fd2), events2_(events2),
          fn_(std::move(fn)) {}

    std::size_t add_poll_fds(std::vector<pollfd> &fds) override;

    void dispatch(const std::vector<pollfd> &fds, std::size_t start) override;

  private:
    int fd_;
    short events_;
    int fd2_;
    short events2_;
    DispatchFn fn_;
};
