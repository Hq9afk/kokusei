#pragma once

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <sys/stat.h>
#include <sys/time.h>

inline FILE *klog_open_file() {
    const char *state_home = getenv("XDG_STATE_HOME");
    std::string base = state_home && *state_home
                           ? std::string(state_home)
                           : std::string(getenv("HOME") ? getenv("HOME") : "") +
                                 "/.local/state";

    std::string dir;
    for (size_t pos = 1; pos <= base.size(); ++pos) {
        if (pos == base.size() || base[pos] == '/') {
            mkdir(base.substr(0, pos).c_str(), 0755);
        }
    }
    dir = base + "/kokusei";
    mkdir(dir.c_str(), 0755);

    return fopen((dir + "/kokusei.log").c_str(), "a");
}

inline void klog(const char *fmt, ...) {
    static FILE *f = klog_open_file();

    timeval tv;
    gettimeofday(&tv, nullptr);
    char timebuf[32];
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S",
             localtime(&tv.tv_sec));

    FILE *outs[2] = {stderr, f};
    for (FILE *out : outs) {
        if (!out)
            continue;
        fprintf(out, "[%s.%03ld] ", timebuf,
               static_cast<long>(tv.tv_usec / 1000));
        va_list args;
        va_start(args, fmt);
        vfprintf(out, fmt, args);
        va_end(args);
        fputc('\n', out);
        fflush(out);
    }
}
