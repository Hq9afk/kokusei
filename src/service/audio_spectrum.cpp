#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <spa/param/audio/format-utils.h>
#include <spa/param/audio/format.h>
#include <spa/param/audio/raw-utils.h>
#include <spa/param/format-utils.h>
#include <spa/pod/builder.h>

#include "config/visualizer_config.h"

#include "core/log.h"

#include "service/audio_spectrum.h"

namespace {

void fft(std::complex<float> *data, int n) {
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(data[i], data[j]);
    }
    for (int len = 2; len <= n; len <<= 1) {
        float angle =
            -2.0f * std::numbers::pi_v<float> / static_cast<float>(len);
        std::complex<float> wn(std::cos(angle), std::sin(angle));
        for (int i = 0; i < n; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            int half = len / 2;
            for (int j = 0; j < half; ++j) {
                std::complex<float> u = data[i + j];
                std::complex<float> v = data[i + j + half] * w;
                data[i + j] = u + v;
                data[i + j + half] = u - v;
                w *= wn;
            }
        }
    }
}

} // namespace

AudioSpectrum::~AudioSpectrum() {
    destroyStream();
    if (loop_)
        pw_thread_loop_stop(loop_);
    if (core_)
        pw_core_disconnect(core_);
    if (context_)
        pw_context_destroy(context_);
    if (loop_)
        pw_thread_loop_destroy(loop_);
}

bool AudioSpectrum::init() {
    for (ChannelPipeline *ch : {&mono_, &left_, &right_}) {
        ch->ring_buf.assign(kSpectrumFftSize, 0.0f);
        ch->fft_buf.resize(kSpectrumFftSize);
        ch->prev_bands.assign(kBars, 0.0f);
        ch->peak.assign(kBars, 0.0f);
        ch->fall.assign(kBars, 0.0f);
        ch->bands.assign(kBars, 0.0f);
        ch->values.assign(kBars, 0.0f);
    }
    window_.resize(kSpectrumFftSize);
    for (int i = 0; i < kSpectrumFftSize; ++i) {
        window_[static_cast<size_t>(i)] =
            0.5f * (1.0f - std::cos(2.0f * std::numbers::pi_v<float> *
                                    static_cast<float>(i) /
                                    static_cast<float>(kSpectrumFftSize - 1)));
    }
    computeBandBins();

    pw_init(nullptr, nullptr);
    loop_ = pw_thread_loop_new("kokusei-visualizer", nullptr);
    if (!loop_) {
        klog("audio_spectrum: failed to create pw_thread_loop");
        return false;
    }
    context_ = pw_context_new(pw_thread_loop_get_loop(loop_), nullptr, 0);
    core_ = pw_context_connect(context_, nullptr, 0);
    if (!core_) {
        klog("audio_spectrum: failed to connect pipewire core");
        return false;
    }
    pw_thread_loop_start(loop_);
    return true;
}

void AudioSpectrum::computeBandBins() {
    bin_low_.resize(kBars);
    bin_high_.resize(kBars);

    float f_low = static_cast<float>(kSpectrumLowerCutoffHz);
    float f_high =
        static_cast<float>(std::min(kSpectrumUpperCutoffHz, sample_rate_ / 2));
    float ratio = f_high / f_low;
    int fft_bins = kSpectrumFftSize / 2;

    for (int i = 0; i < kBars; ++i) {
        float freq_low = f_low * std::pow(ratio, static_cast<float>(i) /
                                                     static_cast<float>(kBars));
        float freq_high =
            f_low * std::pow(ratio, static_cast<float>(i + 1) /
                                        static_cast<float>(kBars));
        int bin_low = static_cast<int>(
            std::ceil(freq_low * static_cast<float>(kSpectrumFftSize) /
                      static_cast<float>(sample_rate_)));
        int bin_high = static_cast<int>(
            std::floor(freq_high * static_cast<float>(kSpectrumFftSize) /
                       static_cast<float>(sample_rate_)));

        bin_low = std::clamp(bin_low, 1, fft_bins);
        bin_high = std::clamp(bin_high, bin_low, fft_bins);
        if (i > 0 && bin_low <= bin_high_[static_cast<size_t>(i - 1)]) {
            bin_low = bin_high_[static_cast<size_t>(i - 1)] + 1;
            if (bin_low > fft_bins)
                bin_low = fft_bins;
            if (bin_high < bin_low)
                bin_high = bin_low;
        }
        bin_low_[static_cast<size_t>(i)] = bin_low;
        bin_high_[static_cast<size_t>(i)] = bin_high;
    }
}

void AudioSpectrum::setTargetNode(uint32_t node_id,
                                  const std::string &node_name) {
    if (node_id == target_node_id_ && node_name == target_node_name_)
        return;
    target_node_id_ = node_id;
    target_node_name_ = node_name;
    destroyStream();
    {
        std::lock_guard<std::mutex> lock(ring_mutex_);
        for (ChannelPipeline *ch : {&mono_, &left_, &right_}) {
            ch->ring_pos = 0;
            ch->ring_full = false;
        }
        samples_received_ = false;
    }
    for (ChannelPipeline *ch : {&mono_, &left_, &right_}) {
        std::fill(ch->prev_bands.begin(), ch->prev_bands.end(), 0.0f);
        std::fill(ch->peak.begin(), ch->peak.end(), 0.0f);
        std::fill(ch->fall.begin(), ch->fall.end(), 0.0f);
        ch->global_max = 1e-3f;
    }
    idle_frames_ = 0;
    if (!target_node_name_.empty())
        buildStream();
}

void AudioSpectrum::buildStream() {
    if (!loop_ || !core_ || target_node_name_.empty())
        return;

    pw_properties *props = pw_properties_new(
        PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Monitor",
        PW_KEY_MEDIA_NAME, "kokusei visualizer", PW_KEY_STREAM_MONITOR, "true",
        PW_KEY_STREAM_CAPTURE_SINK, "true", PW_KEY_NODE_PASSIVE, "true",
        PW_KEY_TARGET_OBJECT, target_node_name_.c_str(), nullptr);

    pw_thread_loop_lock(loop_);

    stream_ = pw_stream_new(core_, "kokusei-spectrum", props);
    static const pw_stream_events events = [] {
        pw_stream_events e{};
        e.version = PW_VERSION_STREAM_EVENTS;
        e.destroy = &AudioSpectrum::onStreamDestroy;
        e.state_changed = &AudioSpectrum::onStateChanged;
        e.param_changed = &AudioSpectrum::onParamChanged;
        e.process = &AudioSpectrum::onProcess;
        return e;
    }();
    pw_stream_add_listener(stream_, &stream_listener_, &events, this);

    std::array<uint8_t, 512> buf{};
    spa_pod_builder b;
    spa_pod_builder_init(&b, buf.data(), static_cast<uint32_t>(buf.size()));
    spa_audio_info_raw raw{};
    raw.format = SPA_AUDIO_FORMAT_F32;
    const spa_pod *params[1];
    params[0] = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &raw);

    pw_stream_connect(stream_, PW_DIRECTION_INPUT, PW_ID_ANY,
                      static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT |
                                                   PW_STREAM_FLAG_MAP_BUFFERS),
                      params, 1);

    pw_thread_loop_unlock(loop_);
}

void AudioSpectrum::destroyStream() {
    if (!stream_ || !loop_)
        return;
    pw_thread_loop_lock(loop_);
    spa_hook_remove(&stream_listener_);
    pw_stream_destroy(stream_);
    stream_ = nullptr;
    format_ready_ = false;
    pw_thread_loop_unlock(loop_);
}

void AudioSpectrum::feedSamples(ChannelPipeline &ch, const float *samples,
                                int count) {
    for (int i = 0; i < count; ++i) {
        ch.ring_buf[static_cast<size_t>(ch.ring_pos)] = samples[i];
        ch.ring_pos = (ch.ring_pos + 1) % kSpectrumFftSize;
        if (ch.ring_pos == 0)
            ch.ring_full = true;
    }
}

void AudioSpectrum::onProcess(void *data) {
    auto *self = static_cast<AudioSpectrum *>(data);
    if (!self->format_ready_ || !self->stream_)
        return;

    pw_buffer *buf = pw_stream_dequeue_buffer(self->stream_);
    if (!buf)
        return;

    spa_buffer *sbuf = buf->buffer;
    if (sbuf && sbuf->n_datas > 0) {
        spa_data *d = &sbuf->datas[0];
        int channels = static_cast<int>(self->format_.channels);
        if (d->data && d->chunk && channels > 0) {
            const auto *base =
                static_cast<const uint8_t *>(d->data) + d->chunk->offset;
            const auto *samples = reinterpret_cast<const float *>(base);
            int frames =
                static_cast<int>(d->chunk->size / sizeof(float)) / channels;
            if (frames > 0) {
                static thread_local std::vector<float> mono;
                static thread_local std::vector<float> left;
                static thread_local std::vector<float> right;
                mono.resize(static_cast<size_t>(frames));
                left.resize(static_cast<size_t>(frames));
                right.resize(static_cast<size_t>(frames));
                if (channels == 1) {
                    std::copy(samples, samples + frames, mono.begin());
                    std::copy(samples, samples + frames, left.begin());
                    std::copy(samples, samples + frames, right.begin());
                } else {
                    float inv = 1.0f / static_cast<float>(channels);
                    for (int i = 0; i < frames; ++i) {
                        float sum = 0.0f;
                        for (int c = 0; c < channels; ++c)
                            sum += samples[i * channels + c];
                        mono[static_cast<size_t>(i)] = sum * inv;
                        left[static_cast<size_t>(i)] = samples[i * channels];
                        right[static_cast<size_t>(i)] =
                            samples[i * channels + 1];
                    }
                }
                std::lock_guard<std::mutex> lock(self->ring_mutex_);
                self->feedSamples(self->mono_, mono.data(), frames);
                self->feedSamples(self->left_, left.data(), frames);
                self->feedSamples(self->right_, right.data(), frames);
                self->samples_received_ = true;
            }
        }
    }
    pw_stream_queue_buffer(self->stream_, buf);
}

void AudioSpectrum::onParamChanged(void *data, uint32_t id,
                                   const spa_pod *param) {
    auto *self = static_cast<AudioSpectrum *>(data);
    if (!param || id != SPA_PARAM_Format)
        return;

    spa_audio_info info{};
    if (spa_format_parse(param, &info.media_type, &info.media_subtype) < 0)
        return;
    if (info.media_type != SPA_MEDIA_TYPE_audio ||
        info.media_subtype != SPA_MEDIA_SUBTYPE_raw)
        return;

    spa_audio_info_raw raw{};
    if (spa_format_audio_raw_parse(param, &raw) < 0 ||
        raw.format != SPA_AUDIO_FORMAT_F32 || raw.channels == 0)
        return;

    self->format_ = raw;
    self->format_ready_ = true;
    self->sample_rate_ = static_cast<int>(raw.rate);
    self->computeBandBins();
}

void AudioSpectrum::onStateChanged(void *data, pw_stream_state,
                                   pw_stream_state state, const char *error) {
    (void)data;
    if (state == PW_STREAM_STATE_ERROR)
        klog("audio_spectrum: stream error: %s", error ? error : "unknown");
}

void AudioSpectrum::onStreamDestroy(void *data) {
    auto *self = static_cast<AudioSpectrum *>(data);
    self->stream_ = nullptr;
    self->format_ready_ = false;
}

void AudioSpectrum::processChannel(ChannelPipeline &ch) {
    for (int i = 0; i < kSpectrumFftSize; ++i) {
        int idx = (ch.ring_pos + i) % kSpectrumFftSize;
        ch.fft_buf[static_cast<size_t>(i)] = {
            ch.ring_buf[static_cast<size_t>(idx)] *
                window_[static_cast<size_t>(i)],
            0.0f};
    }

    fft(ch.fft_buf.data(), kSpectrumFftSize);

    float current_frame_max = 1e-5f;
    for (int i = 0; i < kBars; ++i) {
        float max_mag_sq = 0.0f;
        for (int bin = bin_low_[static_cast<size_t>(i)];
             bin <= bin_high_[static_cast<size_t>(i)]; ++bin) {
            float mag_sq = std::norm(ch.fft_buf[static_cast<size_t>(bin)]);
            if (mag_sq > max_mag_sq)
                max_mag_sq = mag_sq;
        }
        float mag = std::sqrt(max_mag_sq);
        float freq_scale = static_cast<float>(i) /
                           static_cast<float>(kBars > 1 ? kBars - 1 : 1);
        mag *= (2.5f + freq_scale * 4.0f);
        if (freq_scale <= 0.15f)
            mag *= 1.3f;
        ch.bands[static_cast<size_t>(i)] = mag;
        if (mag > current_frame_max)
            current_frame_max = mag;
    }

    ch.global_max = std::max(ch.global_max * 0.995f, current_frame_max);
    float noise_gate = kSpectrumNoiseReduction * 0.01f;
    for (int i = 0; i < kBars; ++i)
        ch.bands[static_cast<size_t>(i)] = std::clamp(
            (ch.bands[static_cast<size_t>(i)] / ch.global_max) - noise_gate,
            0.0f, 1.0f);

    if constexpr (kSpectrumSmoothing) {
        constexpr float kDropOff = 0.66f;
        for (int i = 1; i < kBars; ++i)
            ch.bands[static_cast<size_t>(i)] =
                std::max(ch.bands[static_cast<size_t>(i)],
                         ch.bands[static_cast<size_t>(i - 1)] * kDropOff);
        for (int i = kBars - 2; i >= 0; --i)
            ch.bands[static_cast<size_t>(i)] =
                std::max(ch.bands[static_cast<size_t>(i)],
                         ch.bands[static_cast<size_t>(i + 1)] * kDropOff);
    }

    double gravity_mod =
        std::pow(60.0 / 60.0, 2.5) * 1.54 /
        std::max(static_cast<double>(kSpectrumNoiseReduction), 0.01);
    if (gravity_mod < 1.0)
        gravity_mod = 1.0;

    for (int i = 0; i < kBars; ++i) {
        size_t idx = static_cast<size_t>(i);
        if (ch.bands[idx] < ch.prev_bands[idx]) {
            ch.bands[idx] = std::max(
                static_cast<float>(static_cast<double>(ch.peak[idx]) *
                                   (1.0 - static_cast<double>(ch.fall[idx]) *
                                              static_cast<double>(ch.fall[idx]) *
                                              gravity_mod)),
                0.0f);
            ch.fall[idx] += 0.028f;
        } else {
            ch.peak[idx] = ch.bands[idx];
            ch.fall[idx] = 0.0f;
            ch.bands[idx] =
                ch.prev_bands[idx] + (ch.bands[idx] - ch.prev_bands[idx]) * 0.6f;
        }
        ch.prev_bands[idx] = ch.bands[idx];
    }

    for (int i = 0; i < kBars; ++i)
        ch.values[static_cast<size_t>(i)] = ch.bands[static_cast<size_t>(i)];
}

void AudioSpectrum::processFrame() {
    {
        std::lock_guard<std::mutex> lock(ring_mutex_);
        if (!mono_.ring_full || (idle_ && !samples_received_))
            return;
        if (!samples_received_) {
            for (ChannelPipeline *ch : {&mono_, &left_, &right_})
                for (float &s : ch->ring_buf)
                    s *= 0.85f;
        }
        samples_received_ = false;

        processChannel(mono_);
        processChannel(left_);
        processChannel(right_);
    }

    bool silence = true;
    for (float v : mono_.bands)
        if (v > 0.01f) {
            silence = false;
            break;
        }

    if (silence) {
        ++idle_frames_;
        if (idle_frames_ >= kSpectrumIdleThreshold) {
            if (!idle_) {
                idle_ = true;
                for (ChannelPipeline *ch : {&mono_, &left_, &right_})
                    std::fill(ch->values.begin(), ch->values.end(), 0.0f);
            }
            return;
        }
    } else {
        idle_frames_ = 0;
        idle_ = false;
    }
}
