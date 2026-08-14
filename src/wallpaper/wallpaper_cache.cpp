#include "wallpaper/wallpaper_cache.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>

namespace {

std::string wallpaper_cache_dir() {
    const char *cache_home = getenv("XDG_CACHE_HOME");
    std::string base =
        cache_home && *cache_home
            ? std::string(cache_home)
            : std::string(getenv("HOME") ? getenv("HOME") : "") + "/.cache";
    for (size_t pos = 1; pos <= base.size(); ++pos) {
        if (pos == base.size() || base[pos] == '/')
            mkdir(base.substr(0, pos).c_str(), 0755);
    }
    std::string dir = base + "/kokusei";
    mkdir(dir.c_str(), 0755);
    dir += "/wallpaper";
    mkdir(dir.c_str(), 0755);
    return dir;
}

} // namespace

std::string wallpaper_cache_path(const std::string &path, time_t mtime,
                                 int target_w, int target_h) {
    std::string key = path + ":" + std::to_string(mtime) + ":" +
                      std::to_string(target_w) + "x" + std::to_string(target_h);
    size_t hash = std::hash<std::string>{}(key);
    return wallpaper_cache_dir() + "/" + std::to_string(hash) + ".rgba";
}

unsigned char *wallpaper_cache_read(const std::string &cache_path,
                                    int &out_width, int &out_height) {
    FILE *fp = fopen(cache_path.c_str(), "rb");
    if (!fp)
        return nullptr;
    uint32_t header[2];
    if (fread(header, sizeof(uint32_t), 2, fp) != 2) {
        fclose(fp);
        return nullptr;
    }
    int width = static_cast<int>(header[0]);
    int height = static_cast<int>(header[1]);
    size_t pixel_bytes = static_cast<size_t>(width) * height * 4;
    auto *data = new unsigned char[pixel_bytes];
    size_t read = fread(data, 1, pixel_bytes, fp);
    fclose(fp);
    if (read != pixel_bytes) {
        delete[] data;
        return nullptr;
    }
    out_width = width;
    out_height = height;
    return data;
}

void wallpaper_cache_write(const std::string &cache_path, int width,
                           int height, const unsigned char *data) {
    FILE *fp = fopen(cache_path.c_str(), "wb");
    if (!fp)
        return;
    uint32_t header[2] = {static_cast<uint32_t>(width),
                          static_cast<uint32_t>(height)};
    fwrite(header, sizeof(uint32_t), 2, fp);
    fwrite(data, 1, static_cast<size_t>(width) * height * 4, fp);
    fclose(fp);
}
