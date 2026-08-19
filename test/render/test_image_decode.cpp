#include <cassert>

#include "render/image.h"

static bool has_nonzero_byte(const unsigned char *data, size_t count) {
    for (size_t i = 0; i < count; ++i)
        if (data[i] != 0)
            return true;
    return false;
}

static void test_decode_png() {
    int width = 0, height = 0;
    unsigned char *data =
        load_image_decode(KOKUSEI_DEFAULT_WALLPAPER, width, height);
    assert(data);
    assert(width == 1920);
    assert(height == 1080);
    assert(has_nonzero_byte(data, static_cast<size_t>(width) * height * 4));
    delete[] data;
}

void test_image_decode() { test_decode_png(); }
