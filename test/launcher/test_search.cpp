
#include "../../src/launcher/search.hpp"

#include <cassert>

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
    dir_entry.score =
        900.0f;

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

