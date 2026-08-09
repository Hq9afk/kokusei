
#include "../../src/launcher/launch_action.hpp"

#include <cassert>

void test_launch_action() {

    assert(launch_action_detail::shell_quote("simple") == "'simple'");
    assert(launch_action_detail::shell_quote("it's") == "'it'\\''s'");
    assert(launch_action_detail::shell_quote("a'b'c") == "'a'\\''b'\\''c'");
    assert(launch_action_detail::shell_quote("") == "''");
    assert(launch_action_detail::shell_quote("Bob's Files") ==
           "'Bob'\\''s Files'");

    assert(normalize_url("https://example.com") == "https://example.com");
    assert(normalize_url("//example.com") == "https://example.com");
    assert(normalize_url("localhost") == "http://localhost");
    assert(normalize_url("localhost:8080") == "http://localhost:8080");
    assert(normalize_url("192.168.1.1") == "http://192.168.1.1");
    assert(normalize_url("192.168.1.1:9000/path") ==
           "http://192.168.1.1:9000/path");
    assert(normalize_url("example.com") == "http://example.com");
    assert(normalize_url("host:1234") == "http://host:1234");
    assert(normalize_url("not a url") == "");
    assert(normalize_url("just text") == "");
    assert(normalize_url("") == "");

    assert(make_search_url("hello world", "https://x/?q=") ==
           "https://x/?q=hello%20world");
    assert(make_search_url("a b", "https://x/?q=") == "https://x/?q=a%20b");
    assert(make_search_url("", "https://x/?q=") == "");

    assert(resolve_web_target("example.com", "https://x/?q=") ==
           "http://example.com");
    assert(resolve_web_target("just a query", "https://x/?q=") ==
           "https://x/?q=just%20a%20query");

    assert(launch_non_drun(LauncherMode::Run, "") == false);
    assert(launch_non_drun(LauncherMode::Google, "") == false);
    assert(launch_non_drun(LauncherMode::Drun, "anything") == false);
}

