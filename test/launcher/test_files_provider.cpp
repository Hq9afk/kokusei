
#include "../../src/launcher/files_provider.h"

#include <cassert>
#include <cmath>
#include <unistd.h>

void test_files_provider() {

    assert(to_glob_pattern("notes") == "**/*notes*");
    assert(to_glob_pattern("foo/bar") == "**/foo/bar");
    assert(to_glob_pattern("/abs/path") == "/abs/path");
    assert(to_glob_pattern("**/already") == "**/already");
    assert(to_glob_pattern("") == "");

    {
        auto parts = split_query_parts("hello");
        assert(parts.size() == 1 && parts[0] == "hello");
    }
    {
        auto parts = split_query_parts("foo*bar");
        assert(parts.size() == 2 && parts[0] == "foo" && parts[1] == "bar");
    }
    assert(split_query_parts("").empty());

    assert(score_path("notes.txt", "notes") >
           score_path("my-notes.txt", "notes"));
    assert(score_path("readme.md", "xyz") < 0.0f);
    assert(score_path("foobar.txt", "foo*bar") > 0.0f);
    assert(score_path("barfoo.txt", "foo*bar") < 0.0f);

    assert(score_path("foobarxxxxxxxxxxxxxxxxxxxx", "foo*bar") <
           score_path("fooxxxxxxxxxxxxxxxxxxxxbar", "foo*bar"));

    assert(basename_of("/home/user/file.txt") == "file.txt");
    assert(basename_of("/home/user/") == "user");
    assert(basename_of("/") == "/");

    std::string tmp_dir = "/tmp/kokusei_test_fd_" + std::to_string(getpid());
    system(
        ("mkdir -p " + tmp_dir + "/subdir && touch " + tmp_dir + "/hello.txt")
            .c_str());

    auto files = run_fd_search("**/*hello*", tmp_dir, false, 10);
    assert(!files.empty());
    assert(files[0].name == "hello.txt");

    auto dirs = run_fd_search("**/*subdir*", tmp_dir, true, 10);
    assert(!dirs.empty());
    assert(dirs[0].name == "subdir");

    system(("rm -rf " + tmp_dir).c_str());
}
