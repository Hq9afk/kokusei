
#include "launcher/launcher.h"

#include <cassert>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <sys/wait.h>
#include <unistd.h>

void test_spawn_helpers() {
    std::string marker = "/tmp/kokusei_test_spawn_" + std::to_string(getpid());
    spawn_detached("touch " + marker);

    bool created = false;
    for (int i = 0; i < 50 && !created; ++i) {
        if (access(marker.c_str(), F_OK) == 0)
            created = true;
        else
            usleep(20000);
    }
    assert(created);
    unlink(marker.c_str());

    errno = 0;
    pid_t reaped = waitpid(-1, nullptr, WNOHANG);
    assert(reaped == -1 && errno == ECHILD);

    for (int i = 0; i < 5; ++i)
        spawn_detached("true");
    usleep(50000);
    errno = 0;
    reaped = waitpid(-1, nullptr, WNOHANG);
    assert(reaped == -1 && errno == ECHILD);
}

void test_desktop_entry() {

    assert(strip_exec_field_codes("firefox %u") == "firefox ");
    assert(strip_exec_field_codes("code %F") == "code ");
    assert(strip_exec_field_codes("app --flag") == "app --flag");
    assert(strip_exec_field_codes("echo 100%%") == "echo 100%");
    assert(strip_exec_field_codes("cmd %i %c %k end") == "cmd   end");

    {
        std::istringstream in("[Desktop Entry]\n"
                              "Type=Application\n"
                              "Name=Firefox\n"
                              "Exec=firefox %u\n"
                              "Icon=firefox\n"
                              "Terminal=false\n");
        auto e = desktop_entry_detail::parse_stream(in, "firefox.desktop");
        assert(e.has_value());
        assert(e->name == "Firefox");
        assert(e->exec == "firefox %u");
        assert(e->terminal == false);
        assert(e->no_display == false);
    }
    {
        std::istringstream in("[Desktop Entry]\n"
                              "Type=Application\n"
                              "Name=Hidden Thing\n"
                              "Exec=thing\n"
                              "NoDisplay=true\n");
        auto e = desktop_entry_detail::parse_stream(in, "thing.desktop");
        assert(e.has_value());
        assert(e->no_display == true);
    }
    {

        std::istringstream in("[Desktop Entry]\n"
                              "Type=Link\n"
                              "Name=Some Link\n"
                              "Exec=nothing\n");
        auto e = desktop_entry_detail::parse_stream(in, "link.desktop");
        assert(!e.has_value());
    }
}

void test_visit_store() {
    std::string path = "/tmp/kokusei_test_visits_" + std::to_string(getpid());

    VisitStore vs = visit_store_load(path);
    assert(visit_store_get(vs, visit_store_app_key("firefox.desktop")) == 0);

    visit_store_record(vs, visit_store_app_key("firefox.desktop"));
    visit_store_record(vs, visit_store_app_key("firefox.desktop"));
    visit_store_record(vs, visit_store_file_key("/home/user/notes.txt"));

    assert(visit_store_get(vs, visit_store_app_key("firefox.desktop")) == 2);
    assert(visit_store_get(vs, visit_store_file_key("/home/user/notes.txt")) ==
           1);

    VisitStore reloaded = visit_store_load(path);
    assert(visit_store_get(reloaded, visit_store_app_key("firefox.desktop")) ==
           2);
    assert(visit_store_get(reloaded,
                           visit_store_file_key("/home/user/notes.txt")) == 1);
    assert(visit_store_get(reloaded,
                           visit_store_app_key("never-visited.desktop")) == 0);

    unlink(path.c_str());
}

void test_apps_provider() {
    assert(score_app("Firefox", "fire") > score_app("Bonfire", "fire"));
    assert(std::abs(score_app("nomatch", "xyz") - (-1.0f)) < 0.001f);
    assert(score_app("Firefox", "") < 0.0f);
    assert(score_app("", "firefox") < 0.0f);

    DesktopEntry firefox;
    firefox.id = "firefox.desktop";
    firefox.name = "Firefox";
    firefox.exec = "firefox";

    DesktopEntry bonfire;
    bonfire.id = "bonfire.desktop";
    bonfire.name = "Bonfire";
    bonfire.exec = "bonfire";

    DesktopEntry unrelated;
    unrelated.id = "calc.desktop";
    unrelated.name = "Calculator";
    unrelated.exec = "calc";

    std::vector<DesktopEntry> entries = {firefox, bonfire, unrelated};
    auto results = search_apps(entries, "fire");
    assert(results.size() == 2);

    assert(results[0].entry->name == "Firefox" ||
           results[1].entry->name == "Firefox");
    for (const auto &r : results)
        assert(r.entry->name != "Calculator");
}

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

void test_search() {

    {
        auto r = detect_mode_and_query("firefox");
        assert(r.mode == LauncherMode::Drun);
        assert(r.query == "firefox");
    }
    {
        auto r = detect_mode_and_query("> ls -la");
        assert(r.mode == LauncherMode::Run);
        assert(r.query == "ls -la");
    }
    {
        auto r = detect_mode_and_query("gg cat pictures");
        assert(r.mode == LauncherMode::Google);
        assert(r.query == "cat pictures");
    }
    {

        auto r = detect_mode_and_query("ggwp");
        assert(r.mode == LauncherMode::Drun);
        assert(r.query == "ggwp");
    }
    {
        auto r = detect_mode_and_query("  yt lofi beats");
        assert(r.mode == LauncherMode::YouTube);
        assert(r.query == "lofi beats");
    }
    {
        auto r = detect_mode_and_query("");
        assert(r.mode == LauncherMode::Drun);
        assert(r.query == "");
    }
    {

        auto r = detect_mode_and_query("  firefox");
        assert(r.mode == LauncherMode::Drun);
        assert(r.query == "firefox");
    }

    DesktopEntry app_a;
    app_a.id = "a.desktop";
    app_a.name = "App A";
    DesktopEntry app_b;
    app_b.id = "b.desktop";
    app_b.name = "App B";

    std::vector<ScoredApp> apps = {{&app_a, 500.0f}, {&app_b, 500.0f}};

    FileEntry dir_entry;
    dir_entry.name = "somedir";
    dir_entry.path = "/home/user/somedir";
    dir_entry.is_dir = true;
    dir_entry.score = 900.0f;

    FileEntry file_entry;
    file_entry.name = "somefile.txt";
    file_entry.path = "/home/user/somefile.txt";
    file_entry.is_dir = false;
    file_entry.score = 900.0f;

    std::vector<FileEntry> files = {dir_entry, file_entry};

    VisitStore visits;
    visits.path = "/tmp/kokusei_test_search_unused";
    visit_store_record(visits, visit_store_app_key("b.desktop"));

    auto results = combined_drun_results(apps, files, visits, 10);
    assert(results.size() == 4);
    assert(results[0].kind == DrunResult::Kind::App);
    assert(results[1].kind == DrunResult::Kind::App);

    assert(results[0].app->id == "b.desktop");
    assert(results[1].app->id == "a.desktop");
    assert(results[2].kind == DrunResult::Kind::Dir);
    assert(results[3].kind == DrunResult::Kind::File);

    unlink(visits.path.c_str());
}

void test_submenu() {

    DirLister list_dir = [](const std::string &path, bool want_dirs) {
        std::vector<FileEntry> out;
        if (path == "/root") {
            if (want_dirs) {
                FileEntry d;
                d.name = "sub";
                d.path = "/root/sub";
                d.is_dir = true;
                out.push_back(d);
            } else {
                FileEntry f;
                f.name = "a.txt";
                f.path = "/root/a.txt";
                f.is_dir = false;
                out.push_back(f);
            }
        }
        return out;
    };

    SubmenuState s;

    submenu_open_directory(s, "/root", list_dir);
    assert(s.screen == SubmenuScreen::Browse);
    assert(s.current_path == "/root");
    assert(s.items.size() == 4);

    SubmenuEntry sub = s.items[2];
    assert(sub.action == SubmenuEntry::Action::None && sub.is_dir);
    assert(submenu_handle_entry(s, sub, list_dir));
    assert(s.screen == SubmenuScreen::Browse);
    assert(s.current_path == "/root/sub");

    submenu_open_directory(s, "/root", list_dir);
    SubmenuEntry txt = s.items[3];
    assert(txt.action == SubmenuEntry::Action::None && !txt.is_dir);
    assert(submenu_handle_entry(s, txt, list_dir));
    assert(s.screen == SubmenuScreen::FileActions);
    assert(s.current_path == "/root/a.txt");

    submenu_open_directory(s, "/root", list_dir);

    submenu_open_file_actions(s, "/root/a.txt");
    assert(s.screen == SubmenuScreen::FileActions);
    assert(s.came_from_browse == true);
    assert(s.items.size() == 2);

    bool moved = submenu_go_back(s, list_dir);
    assert(moved);
    assert(s.screen == SubmenuScreen::Browse);
    assert(s.current_path == "/root");

    SubmenuEntry open_options = s.items[0];
    assert(open_options.action == SubmenuEntry::Action::OpenOptions);
    bool handled = submenu_handle_entry(s, open_options, list_dir);
    assert(handled);
    assert(s.screen == SubmenuScreen::DirActions);
    assert(s.items.size() == 3);

    moved = submenu_go_back(s, list_dir);
    assert(moved);
    assert(s.screen == SubmenuScreen::Browse);

    moved = submenu_go_back(s, list_dir);
    assert(moved);
    assert(s.screen == SubmenuScreen::Search);

    moved = submenu_go_back(s, list_dir);
    assert(!moved);

    submenu_open_file_actions(s, "/other/file.txt");
    assert(s.came_from_browse == false);
    moved = submenu_go_back(s, list_dir);
    assert(moved);
    assert(s.screen == SubmenuScreen::Search);
}

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

void test_icon_theme() {
    assert(icon_direct_path("/home/user/icon.png") == "/home/user/icon.png");
    assert(icon_direct_path("/home/user/icon.svg") == "");
    assert(icon_direct_path("firefox") == "");
    assert(icon_direct_path("") == "");
}
