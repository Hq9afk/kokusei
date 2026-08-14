
#include "core/deferred_call.h"

#include <cassert>
#include <poll.h>
#include <thread>

void test_deferred_call() {
    DeferredCall::init();
    assert(DeferredCall::poll_fd() >= 0);

    DeferredCallPollSource source;

    int result = 0;
    std::thread worker([&] { DeferredCall::call_later([&] { result = 42; }); });
    worker.join();

    std::vector<pollfd> fds;
    std::size_t start = fds.size();
    assert(source.add_poll_fds(fds) == 1);
    int ready = poll(fds.data(), fds.size(), 1000);
    assert(ready == 1);
    assert(fds[0].revents & POLLIN);

    source.dispatch(fds, start);
    assert(result == 42);

    bool called_again = false;
    DeferredCall::drain();
    (void)called_again;

    std::vector<int> order;
    DeferredCall::call_later([&] { order.push_back(1); });
    DeferredCall::call_later([&] { order.push_back(2); });
    DeferredCall::drain();
    assert((order == std::vector<int>{1, 2}));
}
