#pragma once

#include "config/visualizer_config.h"

#include <pipewire/pipewire.h>
#include <spa/param/audio/raw.h>
#include <spa/utils/hook.h>

#include <complex>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

class AudioSpectrum {
  public:
    ~AudioSpectrum();

    bool init();

    void setTargetNode(uint32_t node_id, const std::string &node_name);

    void processFrame();

    const std::vector<float> &values() const { return values_; }
    bool idle() const { return idle_; }

  private:
    void buildStream();
    void destroyStream();
    void feedSamples(const float *mono, int count);
    void computeBandBins();

    static void onProcess(void *data);
    static void onParamChanged(void *data, uint32_t id, const spa_pod *param);
    static void onStateChanged(void *data, pw_stream_state old_state,
                               pw_stream_state state, const char *error);
    static void onStreamDestroy(void *data);

    static constexpr int kBars = kVisualizerBarCount;

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
    std::vector<float> ring_buf_;
    int ring_pos_ = 0;
    bool ring_full_ = false;
    bool samples_received_ = false;

    std::vector<float> window_;
    std::vector<int> bin_low_;
    std::vector<int> bin_high_;
    std::vector<float> prev_bands_;
    std::vector<float> peak_;
    std::vector<float> fall_;
    std::vector<float> bands_;
    std::vector<float> values_;
    std::vector<std::complex<float>> fft_buf_;

    float global_max_ = 1e-3f;
    bool idle_ = true;
    int idle_frames_ = 0;
};
