#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <vector>

enum class Easing : uint8_t {
    Linear,
    EaseOutQuad,
    EaseInOutCubic,
    EaseOutCubic,
    EaseOutBack,
    EaseInBack,
};

float applyEasing(Easing easing, float t);

struct Animation {
    float start_value = 0.0f;
    float end_value = 0.0f;
    float duration_ms = 0.0f;
    std::chrono::steady_clock::time_point started_at;
    Easing easing = Easing::EaseOutCubic;
    std::function<void(float)> setter;
    std::function<void()> on_complete;
    bool finished = false;
};

class AnimationManager {
  public:
    using Id = uint32_t;

    Id animate(float from, float to, float duration_ms, Easing easing,
               std::function<void(float)> setter,
               std::function<void()> on_complete = {}, uint64_t owner = 0) {
        return animate_internal(from, to, duration_ms, easing,
                                std::move(setter), std::move(on_complete),
                                owner);
    }

    void cancelForOwner(uint64_t owner);

    void tick(std::chrono::steady_clock::time_point now);

    bool hasActive() const { return !entries_.empty(); }

  private:
    struct Entry {
        Id id = 0;
        uint64_t owner = 0;
        Animation anim;
    };

    Id animate_internal(float from, float to, float duration_ms, Easing easing,
                        std::function<void(float)> setter,
                        std::function<void()> on_complete, uint64_t owner);

    std::vector<Entry> entries_;
    Id next_id_ = 1;
};
