#pragma once

#include "apps_provider.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

struct FileEntry {
    std::string name;
    std::string path;
    bool is_dir = false;
    float score = 0.0f;
};

inline std::string basename_of(const std::string &path) {
    std::string p = path;
    while (p.size() > 1 && p.back() == '/')
        p.pop_back();
    if (p == "/")
        return "/";
    size_t slash = p.find_last_of('/');
    return slash == std::string::npos ? p : p.substr(slash + 1);
}

inline std::string dirname_of(const std::string &path) {
    std::string p = path;
    while (p.size() > 1 && p.back() == '/')
        p.pop_back();
    if (p == "/")
        return "/";
    size_t slash = p.find_last_of('/');
    if (slash == std::string::npos)
        return "";
    return slash == 0 ? "/" : p.substr(0, slash);
}

inline std::string to_glob_pattern(const std::string &query) {
    std::string q = query;

    size_t b = q.find_first_not_of(' ');
    size_t e = q.find_last_not_of(' ');
    q = (b == std::string::npos) ? "" : q.substr(b, e - b + 1);
    if (q.empty())
        return "";
    if (q.starts_with("**/") || q.starts_with("/"))
        return q;
    if (q.find('/') != std::string::npos)
        return "**/" + q;
    return "**/*" + q + "*";
}

inline std::vector<std::string> split_query_parts(const std::string &query) {
    std::string q = to_lower(query);
    size_t b = q.find_first_not_of(' ');
    size_t e = q.find_last_not_of(' ');
    q = (b == std::string::npos) ? "" : q.substr(b, e - b + 1);
    if (q.empty())
        return {};
    if (q.find('*') == std::string::npos)
        return {q};

    std::vector<std::string> parts;
    std::stringstream ss(q);
    std::string part;
    while (std::getline(ss, part, '*')) {
        size_t pb = part.find_first_not_of(' ');
        size_t pe = part.find_last_not_of(' ');
        if (pb == std::string::npos)
            continue;
        parts.push_back(part.substr(pb, pe - pb + 1));
    }
    return parts;
}

inline float score_path(const std::string &name, const std::string &query) {
    std::string n = to_lower(name);
    std::vector<std::string> parts = split_query_parts(query);
    if (n.empty() || parts.empty())
        return -1.0f;

    float score = 0.0f;
    size_t cursor = 0;
    for (size_t i = 0; i < parts.size(); ++i) {
        size_t idx = n.find(parts[i], cursor);
        if (idx == std::string::npos)
            return -1.0f;
        if (i == 0) {
            score += (idx == 0) ? 1000.0f : 500.0f;
            score -= static_cast<float>(std::min(idx, size_t{200}));
        } else {
            size_t end_distance = n.size() - (idx + parts[i].size());
            score += 200.0f;
            score -= static_cast<float>(std::min(end_distance, size_t{200}));
        }
        cursor = idx + parts[i].size();
    }
    score -= static_cast<float>(std::min(n.size(), size_t{200})) / 10.0f;
    return score;
}

namespace files_provider_detail {

inline std::vector<std::string> parse_lines(const std::string &raw) {
    std::vector<std::string> lines;
    std::stringstream ss(raw);
    std::string line;
    while (std::getline(ss, line)) {
        size_t b = line.find_first_not_of(" \t");
        size_t e = line.find_last_not_of(" \t");
        if (b == std::string::npos)
            continue;
        lines.push_back(line.substr(b, e - b + 1));
    }
    return lines;
}

inline std::string run_command(const std::vector<std::string> &argv) {
    std::string cmd;
    for (const auto &a : argv) {
        cmd += '\'';
        for (char c : a) {
            if (c == '\'')
                cmd += "'\\''";
            else
                cmd += c;
        }
        cmd += "' ";
    }
    FILE *pipe = popen(cmd.c_str(), "r");
    if (!pipe)
        return {};
    std::string out;
    std::array<char, 4096> buf{};
    size_t n;
    while ((n = fread(buf.data(), 1, buf.size(), pipe)) > 0)
        out.append(buf.data(), n);
    pclose(pipe);
    return out;
}

}

inline std::vector<std::string> fd_search_argv(const std::string &pattern,
                                               const std::string &search_root,
                                               bool is_dir, int max_results,
                                               int depth = -1,
                                               bool full_path = true) {

    std::vector<std::string> argv = {"fd", "--glob", "--ignore-case"};
    if (full_path)
        argv.push_back("--full-path");
    argv.insert(argv.end(),
                {"--type", is_dir ? "d" : "f", "--hidden", "--no-ignore",
                 "--absolute-path", "--color", "never", "--max-results",
                 std::to_string(max_results)});
    if (depth > 0) {
        argv.push_back("--max-depth");
        argv.push_back(std::to_string(depth));
    }
    argv.push_back("--");
    argv.push_back(pattern.empty() ? "*" : pattern);
    argv.push_back(search_root);
    return argv;
}

inline std::vector<FileEntry> fd_search_parse_output(const std::string &raw,
                                                     bool is_dir) {
    std::vector<FileEntry> results;
    for (const std::string &path : files_provider_detail::parse_lines(raw)) {
        FileEntry fe;
        fe.path = path;
        fe.name = basename_of(path);
        fe.is_dir = is_dir;
        results.push_back(std::move(fe));
    }
    return results;
}

inline std::vector<FileEntry> run_fd_search(const std::string &pattern,
                                            const std::string &search_root,
                                            bool is_dir, int max_results,
                                            int depth = -1,
                                            bool full_path = true) {
    std::string raw = files_provider_detail::run_command(fd_search_argv(
        pattern, search_root, is_dir, max_results, depth, full_path));
    return fd_search_parse_output(raw, is_dir);
}
