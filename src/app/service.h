#pragma once

#include <vector>

#include "core/poll_source.h"

struct WaylandState;

class Service {
  public:
    virtual ~Service() = default;

    virtual const char *name() const = 0;
    virtual bool init(WaylandState &) { return true; }
    virtual void timer_tick(WaylandState &) {}
    virtual std::vector<FnPollSource> poll_sources(WaylandState &) {
        return {};
    }
};
