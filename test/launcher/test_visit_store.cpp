
#include "../../src/launcher/visit_store.hpp"

#include <cassert>
#include <cstdio>
#include <unistd.h>

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

