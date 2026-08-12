#pragma once

#include <string>
#include <vector>

struct FileEntry {
    std::string name;
    std::string path;
    bool is_dir = false;
    float score = 0.0f;
};

std::string basename_of(const std::string &path);

std::string dirname_of(const std::string &path);

std::string to_glob_pattern(const std::string &query);

std::vector<std::string> split_query_parts(const std::string &query);

float score_path(const std::string &name, const std::string &query);

std::vector<std::string> fd_search_argv(const std::string &pattern,
                                        const std::string &search_root,
                                        bool is_dir, int max_results,
                                        int depth = -1, bool full_path = true);

std::vector<FileEntry> fd_search_parse_output(const std::string &raw,
                                              bool is_dir);

std::vector<FileEntry> run_fd_search(const std::string &pattern,
                                     const std::string &search_root,
                                     bool is_dir, int max_results,
                                     int depth = -1, bool full_path = true);
