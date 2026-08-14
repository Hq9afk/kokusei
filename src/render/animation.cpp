#include "render/animation.h"

#include <algorithm>

float applyEasing(Easing easing, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    switch (easing) {
    case Easing::Linear:
        return t;
    case Easing::EaseOutQuad:
        return t * (2.0f - t);
    case Easing::EaseInOutCubic:
        if (t < 0.5f)
            return 4.0f * t * t * t;
        {
            float f = 2.0f * t - 2.0f;
            return 0.5f * f * f * f + 1.0f;
        }
    case Easing::EaseOutCubic: {
        float f = t - 1.0f;
        return f * f * f + 1.0f;
    }
    case Easing::EaseOutBack: {
        constexpr float c1 = 1.70158f;
        constexpr float c3 = c1 + 1.0f;
        float f = t - 1.0f;
        return 1.0f + c3 * f * f * f + c1 * f * f;
    }
    case Easing::EaseInBack: {
        constexpr float c1 = 1.70158f;
        constexpr float c3 = c1 + 1.0f;
        return c3 * t * t * t - c1 * t * t;
    }
    }
    return t;
}

void AnimationManager::cancelForOwner(uint64_t owner) {
    if (owner == 0)
        return;
    std::erase_if(entries_,
                  [owner](const Entry &e) { return e.owner == owner; });
}

void AnimationManager::tick(std::chrono::steady_clock::time_point now) {
    std::vector<std::function<void()>> completed;
    for (Entry &e : entries_) {
        if (e.anim.finished)
            continue;
        float elapsed_ms =
            std::chrono::duration<float, std::milli>(now - e.anim.started_at)
                .count();
        float t =
            e.anim.duration_ms > 0.0f ? elapsed_ms / e.anim.duration_ms : 1.0f;
        if (t >= 1.0f) {
            t = 1.0f;
            e.anim.finished = true;
        }
        float eased = applyEasing(e.anim.easing, t);
        if (e.anim.setter)
            e.anim.setter(e.anim.start_value +
                          (e.anim.end_value - e.anim.start_value) * eased);
        if (e.anim.finished && e.anim.on_complete)
            completed.push_back(std::move(e.anim.on_complete));
    }
    std::erase_if(entries_, [](const Entry &e) { return e.anim.finished; });
    for (auto &cb : completed)
        cb();
}

AnimationManager::Id AnimationManager::animate_internal(
    float from, float to, float duration_ms, Easing easing,
    std::function<void(float)> setter, std::function<void()> on_complete,
    uint64_t owner) {
    cancelForOwner(owner);
    if (duration_ms <= 0.0f) {
        if (setter)
            setter(to);
        if (on_complete)
            on_complete();
        return 0;
    }
    Id id = next_id_++;
    Entry entry;
    entry.id = id;
    entry.owner = owner;
    entry.anim.start_value = from;
    entry.anim.end_value = to;
    entry.anim.duration_ms = duration_ms;
    entry.anim.started_at = std::chrono::steady_clock::now();
    entry.anim.easing = easing;
    entry.anim.setter = std::move(setter);
    entry.anim.on_complete = std::move(on_complete);
    entries_.push_back(std::move(entry));
    return id;
}
