
#include "core/poll_source.h"

#include <cassert>
#include <fcntl.h>
#include <unistd.h>

struct Pipe {
    int r, w;
    Pipe() {
        int fds[2];
        pipe(fds);
        fcntl(fds[0], F_SETFL, O_NONBLOCK);
        r = fds[0];
        w = fds[1];
    }
    void write_byte() {
        char b = 'x';
        write(w, &b, 1);
    }
    void drain() {
        char buf[8];
        while (read(r, buf, sizeof(buf)) > 0) {
        }
    }
    ~Pipe() {
        close(r);
        close(w);
    }
};

void test_poll_source() {

    {
        int calls = 0;
        FnPollSource src(-1, POLLIN, [&] { calls++; });
        std::vector<pollfd> fds;
        assert(src.add_poll_fds(fds) == 0);
        assert(fds.empty());
    }

    {
        Pipe p;
        int calls = 0;
        FnPollSource src(p.r, POLLIN, [&] { calls++; });

        std::vector<pollfd> fds;
        std::size_t start = fds.size();
        assert(src.add_poll_fds(fds) == 1);
        assert(poll(fds.data(), fds.size(), 0) == 0);
        src.dispatch(fds, start);
        assert(calls == 0);

        p.write_byte();
        fds.clear();
        start = fds.size();
        src.add_poll_fds(fds);
        assert(poll(fds.data(), fds.size(), 100) == 1);
        assert(fds[start].revents & POLLIN);
        src.dispatch(fds, start);
        assert(calls == 1);
        p.drain();
    }

    {
        Pipe primary, secondary;
        int calls = 0;
        FnPollSource src(primary.r, POLLIN, secondary.r, POLLIN,
                         [&] { calls++; });

        secondary.write_byte();

        std::vector<pollfd> fds;
        std::size_t start = fds.size();
        assert(src.add_poll_fds(fds) == 2);
        assert(poll(fds.data(), fds.size(), 100) == 1);
        assert(!(fds[start].revents & POLLIN));
        assert(fds[start + 1].revents & POLLIN);
        src.dispatch(fds, start);
        assert(calls == 1);
    }

    {
        Pipe a, b, c;
        int calls_a = 0, calls_b = 0, calls_c = 0;
        FnPollSource src_a(a.r, POLLIN, [&] { calls_a++; });
        FnPollSource src_b(b.r, POLLIN, [&] { calls_b++; });
        FnPollSource src_c(c.r, POLLIN, [&] { calls_c++; });

        a.write_byte();
        c.write_byte();

        std::vector<pollfd> fds;
        struct Range {
            PollSource *src;
            std::size_t start;
        };
        std::vector<Range> ranges;
        for (PollSource *s : {static_cast<PollSource *>(&src_a),
                              static_cast<PollSource *>(&src_b),
                              static_cast<PollSource *>(&src_c)}) {
            std::size_t start = fds.size();
            if (s->add_poll_fds(fds) > 0)
                ranges.push_back({s, start});
        }
        assert(fds.size() == 3);
        assert(poll(fds.data(), fds.size(), 100) == 2);
        for (Range &r : ranges)
            r.src->dispatch(fds, r.start);
        assert(calls_a == 1);
        assert(calls_b == 0);
        assert(calls_c == 1);
    }
}
