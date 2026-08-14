#include "settings/tabs/wallpaper_picker.h"

#include <algorithm>
#include <cassert>
#include <vector>

void test_wallpaper_picker_sort() {
    std::vector<std::string> files = {"/w/b.png", "/w/a.jpg", "/w/A.PNG",
                                      "/w/z.jpg"};
    std::sort(files.begin(), files.end(), wallpaper_picker_less);
    std::vector<std::string> expected = {"/w/a.jpg", "/w/z.jpg", "/w/A.PNG",
                                         "/w/b.png"};
    assert(files == expected);
}
