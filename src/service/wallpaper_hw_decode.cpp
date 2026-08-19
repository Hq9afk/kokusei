#include "core/log.h"

#include "service/wallpaper_hw_decode.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/mem.h>
#include <libavutil/rational.h>
}

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>

namespace {

constexpr AVHWDeviceType kPreferredHwTypes[] = {AV_HWDEVICE_TYPE_CUDA,
                                                AV_HWDEVICE_TYPE_VAAPI};

AVPixelFormat get_hw_format(AVCodecContext *ctx, const AVPixelFormat *pix_fmts) {
    auto *wanted = static_cast<AVPixelFormat *>(ctx->opaque);
    for (const AVPixelFormat *p = pix_fmts; *p != AV_PIX_FMT_NONE; ++p)
        if (*p == *wanted)
            return *p;
    return pix_fmts[0];
}

struct FormatCtxGuard {
    AVFormatContext *ctx = nullptr;
    ~FormatCtxGuard() {
        if (ctx)
            avformat_close_input(&ctx);
    }
};

struct CodecCtxGuard {
    AVCodecContext *ctx = nullptr;
    ~CodecCtxGuard() {
        if (ctx)
            avcodec_free_context(&ctx);
    }
};

struct HwDeviceGuard {
    AVBufferRef *ref = nullptr;
    ~HwDeviceGuard() {
        if (ref)
            av_buffer_unref(&ref);
    }
};

struct FilterGraphGuard {
    AVFilterGraph *graph = nullptr;
    ~FilterGraphGuard() {
        if (graph)
            avfilter_graph_free(&graph);
    }
};

struct FrameGuard {
    AVFrame *frame = av_frame_alloc();
    ~FrameGuard() { av_frame_free(&frame); }
};

struct PacketGuard {
    AVPacket *packet = av_packet_alloc();
    ~PacketGuard() { av_packet_free(&packet); }
};

void decode_loop(std::string path, std::string filter_desc, int fps,
                 WallpaperHwDecodeFrameCallback on_frame,
                 std::shared_ptr<std::atomic<bool>> stop_flag) {
    FormatCtxGuard fmt;
    if (avformat_open_input(&fmt.ctx, path.c_str(), nullptr, nullptr) < 0) {
        klog("wallpaper_hw_decode: open failed '%s'", path.c_str());
        return;
    }
    if (avformat_find_stream_info(fmt.ctx, nullptr) < 0) {
        klog("wallpaper_hw_decode: stream info failed '%s'", path.c_str());
        return;
    }
    int stream_index =
        av_find_best_stream(fmt.ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (stream_index < 0) {
        klog("wallpaper_hw_decode: no video stream '%s'", path.c_str());
        return;
    }
    AVStream *stream = fmt.ctx->streams[stream_index];
    const AVCodec *decoder = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!decoder) {
        klog("wallpaper_hw_decode: no decoder for '%s'", path.c_str());
        return;
    }

    CodecCtxGuard codec;
    codec.ctx = avcodec_alloc_context3(decoder);
    if (!codec.ctx ||
        avcodec_parameters_to_context(codec.ctx, stream->codecpar) < 0) {
        klog("wallpaper_hw_decode: codec context setup failed '%s'", path.c_str());
        return;
    }
    codec.ctx->pkt_timebase = stream->time_base;

    HwDeviceGuard hw_device;
    AVPixelFormat hw_pix_fmt = AV_PIX_FMT_NONE;
    for (AVHWDeviceType type : kPreferredHwTypes) {
        AVPixelFormat candidate = AV_PIX_FMT_NONE;
        for (int i = 0;; ++i) {
            const AVCodecHWConfig *config = avcodec_get_hw_config(decoder, i);
            if (!config)
                break;
            if ((config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) &&
                config->device_type == type) {
                candidate = config->pix_fmt;
                break;
            }
        }
        if (candidate == AV_PIX_FMT_NONE)
            continue;
        if (av_hwdevice_ctx_create(&hw_device.ref, type, nullptr, nullptr, 0) == 0) {
            hw_pix_fmt = candidate;
            break;
        }
    }
    if (hw_device.ref) {
        codec.ctx->hw_device_ctx = av_buffer_ref(hw_device.ref);
        codec.ctx->opaque = &hw_pix_fmt;
        codec.ctx->get_format = get_hw_format;
        klog("wallpaper_hw_decode: hw decode enabled for '%s'", path.c_str());
    }

    if (avcodec_open2(codec.ctx, decoder, nullptr) < 0) {
        klog("wallpaper_hw_decode: avcodec_open2 failed '%s'", path.c_str());
        return;
    }

    FilterGraphGuard filter;
    AVFilterContext *buffersrc_ctx = nullptr;
    AVFilterContext *buffersink_ctx = nullptr;

    auto ensure_filter_graph = [&](const AVFrame *sample) -> bool {
        if (filter.graph)
            return true;
        filter.graph = avfilter_graph_alloc();
        const AVFilter *buffersrc = avfilter_get_by_name("buffer");
        const AVFilter *buffersink = avfilter_get_by_name("buffersink");
        char args[256];
        AVRational time_base = stream->time_base;
        AVRational aspect = sample->sample_aspect_ratio.num
                               ? sample->sample_aspect_ratio
                               : AVRational{1, 1};
        std::snprintf(
            args, sizeof(args),
            "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
            sample->width, sample->height, sample->format, time_base.num,
            time_base.den, aspect.num, aspect.den);
        if (avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in", args,
                                         nullptr, filter.graph) < 0 ||
            avfilter_graph_create_filter(&buffersink_ctx, buffersink, "out",
                                         nullptr, nullptr, filter.graph) < 0) {
            klog("wallpaper_hw_decode: filter source/sink setup failed '%s'",
                path.c_str());
            return false;
        }
        AVFilterInOut *outputs = avfilter_inout_alloc();
        AVFilterInOut *inputs = avfilter_inout_alloc();
        outputs->name = av_strdup("in");
        outputs->filter_ctx = buffersrc_ctx;
        outputs->pad_idx = 0;
        outputs->next = nullptr;
        inputs->name = av_strdup("out");
        inputs->filter_ctx = buffersink_ctx;
        inputs->pad_idx = 0;
        inputs->next = nullptr;
        int ret = avfilter_graph_parse_ptr(filter.graph, filter_desc.c_str(),
                                           &inputs, &outputs, nullptr);
        avfilter_inout_free(&inputs);
        avfilter_inout_free(&outputs);
        if (ret < 0 || avfilter_graph_config(filter.graph, nullptr) < 0) {
            klog("wallpaper_hw_decode: filter graph '%s' failed for '%s'",
                filter_desc.c_str(), path.c_str());
            return false;
        }
        return true;
    };

    FrameGuard filtered;

    auto deliver_frame = [&](AVFrame *sw_frame) {
        if (!ensure_filter_graph(sw_frame))
            return;
        if (av_buffersrc_add_frame_flags(buffersrc_ctx, sw_frame, 0) < 0)
            return;
        while (av_buffersink_get_frame(buffersink_ctx, filtered.frame) >= 0) {
            int w = filtered.frame->width;
            int h = filtered.frame->height;
            size_t row_bytes = static_cast<size_t>(w) * 4;
            auto *copy = new unsigned char[row_bytes * static_cast<size_t>(h)];
            for (int y = 0; y < h; ++y)
                std::memcpy(
                    copy + static_cast<size_t>(y) * row_bytes,
                    filtered.frame->data[0] +
                        static_cast<size_t>(y) * filtered.frame->linesize[0],
                    row_bytes);
            on_frame(copy, w, h);
            av_frame_unref(filtered.frame);
        }
    };

    PacketGuard packet;
    FrameGuard decoded;
    FrameGuard downloaded;

    using clock = std::chrono::steady_clock;
    auto frame_interval = std::chrono::duration<double>(1.0 / std::max(1, fps));
    auto next_frame_due = clock::now();

    auto drain_available_frames = [&]() -> bool {
        for (;;) {
            int recv_ret = avcodec_receive_frame(codec.ctx, decoded.frame);
            if (recv_ret < 0)
                return true;

            AVFrame *sw_frame = decoded.frame;
            if (hw_pix_fmt != AV_PIX_FMT_NONE &&
                decoded.frame->format == hw_pix_fmt) {
                if (av_hwframe_transfer_data(downloaded.frame, decoded.frame, 0) <
                    0) {
                    av_frame_unref(decoded.frame);
                    continue;
                }
                sw_frame = downloaded.frame;
            }
            deliver_frame(sw_frame);
            av_frame_unref(downloaded.frame);
            av_frame_unref(decoded.frame);

            if (stop_flag->load())
                return false;

            next_frame_due +=
                std::chrono::duration_cast<clock::duration>(frame_interval);
            auto now = clock::now();
            if (next_frame_due < now)
                next_frame_due = now;
            while (!stop_flag->load() && clock::now() < next_frame_due) {
                auto remaining = next_frame_due - clock::now();
                auto step = remaining < std::chrono::milliseconds(20)
                              ? remaining
                              : std::chrono::duration_cast<clock::duration>(
                                    std::chrono::milliseconds(20));
                std::this_thread::sleep_for(step);
            }
            if (stop_flag->load())
                return false;
        }
    };

    while (!stop_flag->load()) {
        int read_ret = av_read_frame(fmt.ctx, packet.packet);
        if (read_ret < 0) {
            avcodec_send_packet(codec.ctx, nullptr);
            if (!drain_available_frames())
                return;
            if (av_seek_frame(fmt.ctx, stream_index, 0, AVSEEK_FLAG_BACKWARD) < 0) {
                klog("wallpaper_hw_decode: loop seek failed '%s'", path.c_str());
                return;
            }
            avcodec_flush_buffers(codec.ctx);
            continue;
        }
        if (packet.packet->stream_index != stream_index) {
            av_packet_unref(packet.packet);
            continue;
        }
        int send_ret = avcodec_send_packet(codec.ctx, packet.packet);
        av_packet_unref(packet.packet);
        if (send_ret < 0)
            continue;

        if (!drain_available_frames())
            return;
    }
}

} // namespace

WallpaperHwDecodePlayback
wallpaper_hw_decode_start(const std::string &path, const std::string &filter_desc,
                          int fps, WallpaperHwDecodeFrameCallback on_frame) {
    WallpaperHwDecodePlayback playback;
    playback.stop_flag = std::make_shared<std::atomic<bool>>(false);
    playback.worker = std::thread(decode_loop, path, filter_desc, fps,
                                  std::move(on_frame), playback.stop_flag);
    return playback;
}

void wallpaper_hw_decode_stop(WallpaperHwDecodePlayback &playback) {
    if (!playback.stop_flag)
        return;
    playback.stop_flag->store(true);
    if (playback.worker.joinable())
        playback.worker.join();
    playback.stop_flag.reset();
}
