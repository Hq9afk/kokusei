
#include "../../src/launcher/desktop_entry.h"

#include <cassert>
#include <sstream>

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
