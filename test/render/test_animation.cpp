
#include "render/animation.h"

#include <cassert>
#include <cmath>

static void test_easing() {
    for (Easing e :
         {Easing::Linear, Easing::EaseOutQuad, Easing::EaseInOutCubic,
          Easing::EaseOutCubic, Easing::EaseOutBack, Easing::EaseInBack}) {
        assert(std::fabs(applyEasing(e, 0.0f) - 0.0f) < 1e-5f);
        assert(std::fabs(applyEasing(e, 1.0f) - 1.0f) < 1e-5f);
    }
    assert(applyEasing(Easing::EaseOutQuad, 0.5f) > 0.5f);
    assert(applyEasing(Easing::EaseInBack, 0.5f) < 0.0f);
}

static void test_interpolation() {
    AnimationManager mgr;
    float value = -1.0f;
    int completions = 0;

    auto now = std::chrono::steady_clock::now();
    mgr.animate(
        0.0f, 10.0f, 100.0f, Easing::Linear, [&](float v) { value = v; },
        [&] { completions++; });

    mgr.tick(now);
    assert(value == 0.0f);

    mgr.tick(now + std::chrono::milliseconds(50));
    assert(std::fabs(value - 5.0f) < 1e-3f);
    assert(completions == 0);
    assert(mgr.hasActive());

    mgr.tick(now + std::chrono::milliseconds(150));
    assert(std::fabs(value - 10.0f) < 1e-3f);
    assert(completions == 1);
    assert(!mgr.hasActive());
}

static void test_zero_duration() {
    AnimationManager mgr;
    float value = -1.0f;
    int completions = 0;
    mgr.animate(
        0.0f, 5.0f, 0.0f, Easing::Linear, [&](float v) { value = v; },
        [&] { completions++; });
    assert(value == 5.0f);
    assert(completions == 1);
    assert(!mgr.hasActive());
}

static void test_cancel_for_owner() {
    AnimationManager mgr;
    float value = -1.0f;
    bool completed = false;
    auto now = std::chrono::steady_clock::now();
    mgr.animate(
        0.0f, 10.0f, 100.0f, Easing::Linear, [&](float v) { value = v; },
        [&] { completed = true; }, 1);

    mgr.cancelForOwner(1);
    assert(!mgr.hasActive());

    mgr.tick(now + std::chrono::milliseconds(200));
    assert(value == -1.0f);
    assert(!completed);
}

static void test_owner_reuse_cancels_prior() {
    AnimationManager mgr;
    float first = -1.0f;
    float second = -1.0f;
    auto now = std::chrono::steady_clock::now();

    mgr.animate(
        0.0f, 10.0f, 100.0f, Easing::Linear, [&](float v) { first = v; }, {},
        1);
    mgr.animate(
        0.0f, 20.0f, 100.0f, Easing::Linear, [&](float v) { second = v; }, {},
        1);

    mgr.tick(now + std::chrono::milliseconds(150));
    assert(first == -1.0f);
    assert(second == 20.0f);
}

void test_animation() {
    test_easing();
    test_interpolation();
    test_zero_duration();
    test_cancel_for_owner();
    test_owner_reuse_cancels_prior();
}
