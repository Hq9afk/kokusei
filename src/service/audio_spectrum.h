#pragma once

#include <complex>
#include <cstdint>
#include <mutex>
#include <pipewire/pipewire.h>
#include <spa/param/audio/raw.h>
#include <spa/utils/hook.h>
#include <string>
#include <vector>

class AudioSpectrum {
  public:
    ~AudioSpectrum();

    bool init();

    void setTargetNode(uint32_t node_id, const std::string &node_name);

    void processFrame();

    void setBarCount(int count);

    const std::vector<float> &values() const { return mono_.values; }
    const std::vector<float> &valuesLeft() const { return left_.values; }
    const std::vector<float> &valuesRight() const { return right_.values; }
    bool idle() const { return idle_; }

  private:
    struct ChannelPipeline {
        std::vector<float> ring_buf;
        int ring_pos = 0;
        bool ring_full = false;

        std::vector<float> prev_bands;
        std::vector<float> peak;
        std::vector<float> fall;
        std::vector<float> bands;
        std::vector<float> values;
        std::vector<std::complex<float>> fft_buf;

        float global_max = 1e-3f;
    };

    void buildStream();
    void destroyStream();
    void feedSamples(ChannelPipeline &ch, const float *samples, int count);
    void computeBandBinsFor(int bars, std::vector<int> &bin_low,
                            std::vector<int> &bin_high);
    void processChannel(ChannelPipeline &ch, const std::vector<int> &bin_low,
                        const std::vector<int> &bin_high);

    static void onProcess(void *data);
    static void onParamChanged(void *data, uint32_t id, const spa_pod *param);
    static void onStateChanged(void *data, pw_stream_state old_state,
                               pw_stream_state state, const char *error);
    static void onStreamDestroy(void *data);

    uint32_t target_node_id_ = 0;
    std::string target_node_name_;

    pw_thread_loop *loop_ = nullptr;
    pw_context *context_ = nullptr;
    pw_core *core_ = nullptr;
    pw_stream *stream_ = nullptr;
    spa_hook stream_listener_{};
    bool format_ready_ = false;
    spa_audio_info_raw format_{};
    int sample_rate_ = 48000;

    std::mutex ring_mutex_;
    bool samples_received_ = false;

    std::vector<float> window_;
    std::vector<int> bin_low_lr_;
    std::vector<int> bin_high_lr_;
    std::vector<int> bin_low_mono_;
    std::vector<int> bin_high_mono_;
    int mono_bar_count_ = 0;

    ChannelPipeline mono_;
    ChannelPipeline left_;
    ChannelPipeline right_;

    bool idle_ = true;
    int idle_frames_ = 0;
};
