#pragma once

#include "poll_source.hpp"

#include <sdbus-c++/sdbus-c++.h>

inline FnPollSource sdbus_poll_source(sdbus::IConnection &bus,
                                      FnPollSource::DispatchFn on_ready) {
    sdbus::IConnection::PollData pd = bus.getEventLoopPollData();
    if (pd.eventFd >= 0)
        return FnPollSource(pd.fd, pd.events, pd.eventFd, POLLIN,
                            std::move(on_ready));
    return FnPollSource(pd.fd, pd.events, std::move(on_ready));
}
