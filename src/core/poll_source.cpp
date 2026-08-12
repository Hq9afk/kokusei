#include "poll_source.h"

std::size_t FnPollSource::add_poll_fds(std::vector<pollfd> &fds) {
    if (fd_ < 0)
        return 0;
    fds.push_back({.fd = fd_, .events = events_, .revents = 0});
    if (fd2_ < 0)
        return 1;
    fds.push_back({.fd = fd2_, .events = events2_, .revents = 0});
    return 2;
}

void FnPollSource::dispatch(const std::vector<pollfd> &fds, std::size_t start) {
    bool primary = fds[start].revents & POLLIN;
    bool secondary = fd2_ >= 0 && (fds[start + 1].revents & POLLIN);
    if (primary || secondary)
        fn_();
}
