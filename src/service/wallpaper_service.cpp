#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>

#include "core/log.h"

#include "render/image.h"

#include "service/wallpaper_service.h"

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

void wallpaper_cache_write(const std::string &cache_path, int width, int height,
                           const unsigned char *data) {
    FILE *fp = fopen(cache_path.c_str(), "wb");
    if (!fp)
        return;
    uint32_t header[2] = {static_cast<uint32_t>(width),
                          static_cast<uint32_t>(height)};
    fwrite(header, sizeof(uint32_t), 2, fp);
    fwrite(data, 1, static_cast<size_t>(width) * height * 4, fp);
    fclose(fp);
}

void box_downsample_rgba(const unsigned char *src, int sw, int sh,
                         unsigned char *dst, int dw, int dh) {
    for (int y = 0; y < dh; ++y) {
        int y0 = static_cast<int>(static_cast<int64_t>(y) * sh / dh);
        int y1 = static_cast<int>(static_cast<int64_t>(y + 1) * sh / dh);
        y1 = std::max(y1, y0 + 1);
        for (int x = 0; x < dw; ++x) {
            int x0 = static_cast<int>(static_cast<int64_t>(x) * sw / dw);
            int x1 = static_cast<int>(static_cast<int64_t>(x + 1) * sw / dw);
            x1 = std::max(x1, x0 + 1);
            long sum[4] = {0, 0, 0, 0};
            int count = 0;
            for (int sy = y0; sy < y1 && sy < sh; ++sy) {
                const unsigned char *row =
                    src + static_cast<size_t>(sy) * sw * 4;
                for (int sx = x0; sx < x1 && sx < sw; ++sx) {
                    const unsigned char *px = row + static_cast<size_t>(sx) * 4;
                    sum[0] += px[0];
                    sum[1] += px[1];
                    sum[2] += px[2];
                    sum[3] += px[3];
                    ++count;
                }
            }
            count = std::max(count, 1);
            unsigned char *out = dst + (static_cast<size_t>(y) * dw + x) * 4;
            out[0] = static_cast<unsigned char>(sum[0] / count);
            out[1] = static_cast<unsigned char>(sum[1] / count);
            out[2] = static_cast<unsigned char>(sum[2] / count);
            out[3] = static_cast<unsigned char>(sum[3] / count);
        }
    }
}

} // namespace

unsigned char *wallpaper_decode_scaled(const std::string &path, int target_w,
                                       int target_h, int &out_width,
                                       int &out_height) {
    struct stat st{};
    bool cacheable =
        target_w > 0 && target_h > 0 && stat(path.c_str(), &st) == 0;
    std::string cache_path;
    if (cacheable) {
        cache_path =
            wallpaper_cache_path(path, st.st_mtime, target_w, target_h);
        if (unsigned char *cached =
                wallpaper_cache_read(cache_path, out_width, out_height)) {
            klog("wallpaper: cache hit '%s' (%dx%d)", path.c_str(), out_width,
                 out_height);
            return cached;
        }
    }

    unsigned char *data = load_image_decode(path, out_width, out_height);
    if (!data)
        return nullptr;
    if (target_w <= 0 || target_h <= 0)
        return data;
    float scale = std::max(static_cast<float>(target_w) / out_width,
                           static_cast<float>(target_h) / out_height);
    unsigned char *result = data;
    if (scale < 1.0f) {
        int dw = std::max(1, static_cast<int>(std::lround(out_width * scale)));
        int dh = std::max(1, static_cast<int>(std::lround(out_height * scale)));
        auto *scaled = new unsigned char[static_cast<size_t>(dw) * dh * 4];
        box_downsample_rgba(data, out_width, out_height, scaled, dw, dh);
        delete[] data;
        out_width = dw;
        out_height = dh;
        result = scaled;
    }
    if (cacheable)
        wallpaper_cache_write(cache_path, out_width, out_height, result);
    return result;
}

int wallpaper_service_column_count(const Config &cfg,
                                   const std::string &monitor_name) {
    auto it = cfg.wallpaper_column_counts.find(monitor_name);
    return it != cfg.wallpaper_column_counts.end() && it->second > 0
               ? it->second
               : 1;
}

std::string wallpaper_service_column_override(const Config &cfg,
                                              const std::string &monitor_name,
                                              int column_index) {
    auto it = cfg.wallpaper_columns.find(monitor_name);
    if (it != cfg.wallpaper_columns.end() && column_index >= 0 &&
        static_cast<size_t>(column_index) < it->second.size())
        return it->second[static_cast<size_t>(column_index)];
    return "";
}

std::string wallpaper_service_column_path(const Config &cfg,
                                          const std::string &monitor_name,
                                          int column_index) {
    std::string override =
        wallpaper_service_column_override(cfg, monitor_name, column_index);
    if (!override.empty())
        return override;
    return cfg.default_wallpaper_enabled ? cfg.wallpaper_path : "";
}

std::string wallpaper_service_fill_mode(const Config &cfg,
                                        const std::string &monitor_name,
                                        int column_index) {
    auto it = cfg.wallpaper_fill_modes.find(monitor_name);
    if (it != cfg.wallpaper_fill_modes.end() && column_index >= 0 &&
        static_cast<size_t>(column_index) < it->second.size() &&
        !it->second[static_cast<size_t>(column_index)].empty())
        return it->second[static_cast<size_t>(column_index)];
    return "crop";
}

int wallpaper_service_animated_column_count(const Config &cfg,
                                            const std::string &monitor_name) {
    auto it = cfg.wallpaper_animated_column_counts.find(monitor_name);
    return it != cfg.wallpaper_animated_column_counts.end() && it->second > 0
               ? it->second
               : 1;
}

std::string wallpaper_service_animated_column_override(
    const Config &cfg, const std::string &monitor_name, int column_index) {
    auto it = cfg.wallpaper_animated_columns.find(monitor_name);
    if (it != cfg.wallpaper_animated_columns.end() && column_index >= 0 &&
        static_cast<size_t>(column_index) < it->second.size())
        return it->second[static_cast<size_t>(column_index)];
    return "";
}

std::string wallpaper_service_animated_column_path(
    const Config &cfg, const std::string &monitor_name, int column_index) {
    std::string override = wallpaper_service_animated_column_override(
        cfg, monitor_name, column_index);
    if (!override.empty())
        return override;
    return cfg.default_wallpaper_enabled ? cfg.wallpaper_path : "";
}

std::string wallpaper_service_animated_fill_mode(
    const Config &cfg, const std::string &monitor_name, int column_index) {
    auto it = cfg.wallpaper_animated_fill_modes.find(monitor_name);
    if (it != cfg.wallpaper_animated_fill_modes.end() && column_index >= 0 &&
        static_cast<size_t>(column_index) < it->second.size() &&
        !it->second[static_cast<size_t>(column_index)].empty())
        return it->second[static_cast<size_t>(column_index)];
    return "crop";
}
