#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

#include "core/log.h"

#include "service/toplevel_export.h"

namespace {

uint32_t parse_handle(const std::string &address) {
    return static_cast<uint32_t>(std::strtoull(address.c_str(), nullptr, 16));
}

void swizzle_bgra_to_rgba(const uint8_t *src, uint32_t src_stride,
                          uint32_t width, uint32_t height,
                          std::vector<uint8_t> &dst) {
    dst.resize(static_cast<size_t>(width) * height * 4);
    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t *row = src + static_cast<size_t>(y) * src_stride;
        uint8_t *out = dst.data() + static_cast<size_t>(y) * width * 4;
        for (uint32_t x = 0; x < width; ++x) {
            out[x * 4 + 0] = row[x * 4 + 2];
            out[x * 4 + 1] = row[x * 4 + 1];
            out[x * 4 + 2] = row[x * 4 + 0];
            out[x * 4 + 3] = row[x * 4 + 3];
        }
    }
}

void unmap_and_destroy_buffer(ToplevelExportCapture &cap) {
    if (cap.buffer)
        wl_buffer_destroy(cap.buffer);
    cap.buffer = nullptr;
    if (cap.shm_data && cap.shm_size)
        munmap(cap.shm_data, cap.shm_size);
    cap.shm_data = nullptr;
    cap.shm_size = 0;
}

bool allocate_buffer(ToplevelExportCapture &cap, wl_shm *shm, uint32_t width,
                     uint32_t height, uint32_t stride, uint32_t format) {
    unmap_and_destroy_buffer(cap);

    size_t size = static_cast<size_t>(stride) * height;
    int fd = memfd_create("kokusei-toplevel-export", MFD_CLOEXEC);
    if (fd < 0) {
        klog("toplevel_export: memfd_create failed: %s", strerror(errno));
        return false;
    }
    if (ftruncate(fd, static_cast<off_t>(size)) != 0) {
        klog("toplevel_export: ftruncate failed: %s", strerror(errno));
        close(fd);
        return false;
    }
    void *data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        klog("toplevel_export: mmap failed: %s", strerror(errno));
        close(fd);
        return false;
    }

    wl_shm_pool *pool = wl_shm_create_pool(shm, fd, static_cast<int32_t>(size));
    close(fd);
    if (!pool) {
        munmap(data, size);
        return false;
    }
    cap.buffer = wl_shm_pool_create_buffer(
        pool, 0, static_cast<int32_t>(width), static_cast<int32_t>(height),
        static_cast<int32_t>(stride), format);
    wl_shm_pool_destroy(pool);
    if (!cap.buffer) {
        munmap(data, size);
        return false;
    }

    cap.shm_data = data;
    cap.shm_size = size;
    cap.buf_width = width;
    cap.buf_height = height;
    cap.buf_stride = stride;
    cap.buf_format = format;
    return true;
}

void handle_buffer(void *data, hyprland_toplevel_export_frame_v1 *,
                   uint32_t format, uint32_t width, uint32_t height,
                   uint32_t stride) {
    auto *cap = static_cast<ToplevelExportCapture *>(data);
    cap->pending_width = width;
    cap->pending_height = height;
    cap->pending_stride = stride;
    cap->pending_format = format;
    cap->have_pending_shm_format = true;
}

void handle_linux_dmabuf(void *, hyprland_toplevel_export_frame_v1 *, uint32_t,
                         uint32_t, uint32_t) {}

void handle_flags(void *data, hyprland_toplevel_export_frame_v1 *,
                  uint32_t flags) {
    auto *cap = static_cast<ToplevelExportCapture *>(data);
    cap->y_invert =
        (flags & HYPRLAND_TOPLEVEL_EXPORT_FRAME_V1_FLAGS_Y_INVERT) != 0;
}

void handle_damage(void *, hyprland_toplevel_export_frame_v1 *, uint32_t,
                   uint32_t, uint32_t, uint32_t) {}

void handle_buffer_done(void *data, hyprland_toplevel_export_frame_v1 *frame) {
    auto *cap = static_cast<ToplevelExportCapture *>(data);
    if (!cap->have_pending_shm_format) {
        hyprland_toplevel_export_frame_v1_destroy(frame);
        cap->frame = nullptr;
        cap->in_flight = false;
        return;
    }
    cap->have_pending_shm_format = false;

    bool need_realloc = !cap->buffer || cap->buf_width != cap->pending_width ||
                        cap->buf_height != cap->pending_height ||
                        cap->buf_stride != cap->pending_stride ||
                        cap->buf_format != cap->pending_format;
    if (need_realloc) {
        if (!allocate_buffer(*cap, cap->shm, cap->pending_width,
                             cap->pending_height, cap->pending_stride,
                             cap->pending_format)) {
            hyprland_toplevel_export_frame_v1_destroy(frame);
            cap->frame = nullptr;
            cap->in_flight = false;
            return;
        }
    }

    hyprland_toplevel_export_frame_v1_copy(frame, cap->buffer, 1);
}

void handle_ready(void *data, hyprland_toplevel_export_frame_v1 *frame,
                  uint32_t, uint32_t, uint32_t) {
    auto *cap = static_cast<ToplevelExportCapture *>(data);
    static std::vector<uint8_t> scratch;
    swizzle_bgra_to_rgba(static_cast<const uint8_t *>(cap->shm_data),
                         cap->buf_stride, cap->buf_width, cap->buf_height,
                         scratch);
    update_texture_rgba(cap->tex, static_cast<int>(cap->buf_width),
                        static_cast<int>(cap->buf_height), scratch.data());
    cap->last_capture = std::chrono::steady_clock::now();
    hyprland_toplevel_export_frame_v1_destroy(frame);
    cap->frame = nullptr;
    cap->in_flight = false;
}

void handle_failed(void *data, hyprland_toplevel_export_frame_v1 *frame) {
    auto *cap = static_cast<ToplevelExportCapture *>(data);
    hyprland_toplevel_export_frame_v1_destroy(frame);
    cap->frame = nullptr;
    cap->in_flight = false;
}

const hyprland_toplevel_export_frame_v1_listener &frame_listener() {
    static constexpr hyprland_toplevel_export_frame_v1_listener l{
        .buffer = handle_buffer,
        .damage = handle_damage,
        .flags = handle_flags,
        .ready = handle_ready,
        .failed = handle_failed,
        .linux_dmabuf = handle_linux_dmabuf,
        .buffer_done = handle_buffer_done,
    };
    return l;
}

} // namespace

void toplevel_export_request(ToplevelExportState &state,
                             hyprland_toplevel_export_manager_v1 *manager,
                             wl_shm *shm, const std::string &address,
                             int min_interval_ms) {
    if (!manager || !shm)
        return;

    ToplevelExportCapture &cap = state.captures[address];
    if (cap.in_flight)
        return;
    if (cap.last_capture.time_since_epoch().count() != 0) {
        auto elapsed = std::chrono::steady_clock::now() - cap.last_capture;
        if (elapsed < std::chrono::milliseconds(min_interval_ms))
            return;
    }

    uint32_t handle = parse_handle(address);
    cap.shm = shm;
    cap.frame = hyprland_toplevel_export_manager_v1_capture_toplevel(manager, 0,
                                                                     handle);
    if (!cap.frame)
        return;
    cap.in_flight = true;
    hyprland_toplevel_export_frame_v1_add_listener(cap.frame, &frame_listener(),
                                                   &cap);
}

const Texture *toplevel_export_texture(const ToplevelExportState &state,
                                       const std::string &address) {
    auto it = state.captures.find(address);
    if (it == state.captures.end() || !it->second.tex.id)
        return nullptr;
    return &it->second.tex;
}

void toplevel_export_prune(ToplevelExportState &state,
                           const std::vector<std::string> &live_addresses) {
    for (auto it = state.captures.begin(); it != state.captures.end();) {
        bool live = false;
        for (const auto &addr : live_addresses)
            if (addr == it->first) {
                live = true;
                break;
            }
        if (live) {
            ++it;
            continue;
        }
        unmap_and_destroy_buffer(it->second);
        it = state.captures.erase(it);
    }
}
