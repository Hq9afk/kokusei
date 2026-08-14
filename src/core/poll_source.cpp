#include "core/poll_source.h"

#include <sdbus-c++/sdbus-c++.h>

FnPollSource sdbus_poll_source(sdbus::IConnection &bus,
                               FnPollSource::DispatchFn on_ready) {
    sdbus::IConnection::PollData pd = bus.getEventLoopPollData();
    if (pd.eventFd >= 0)
        return FnPollSource(pd.fd, pd.events, pd.eventFd, POLLIN,
                            std::move(on_ready));
    return FnPollSource(pd.fd, pd.events, std::move(on_ready));
}

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
