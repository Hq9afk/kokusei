
#include "../../src/launcher/submenu.hpp"

#include <cassert>

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

