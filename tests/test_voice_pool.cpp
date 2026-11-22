#include "sfx/voice_pool.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>

using namespace vfx;

namespace {
struct Fixture {
    ClipBank bank;
    std::unique_ptr<Clip> clip = std::make_unique<Clip>();
    VoicePool pool{bank};
    float L[512], R[512];
    Fixture(size_t frames = 1000) {
        clip->left.assign(frames, 0.5f);
        clip->right.assign(frames, 0.5f);
        bank.entries[3].store(clip.get());
        pool.setOutputRate(48000.f);
    }
    int render(int n = 512) {
        std::fill(L, L + n, 0.f); std::fill(R, R + n, 0.f);
        return pool.render(L, R, n);
    }
    void play(PlayMode m, int slot = 3) { pool.apply({Command::Play, m, int16_t(slot), 1.f}); }
};
} // namespace

TEST_CASE("one-shot plays once and finishes") {
    Fixture f;
    f.play(PlayMode::Restart);
    REQUIRE(f.render() == 1);
    REQUIRE(f.L[0] == 0.5f);
    f.render();       // frames 512..1023 -> clip ends
    REQUIRE(f.render() == 0);
}

TEST_CASE("restart mode keeps one copy, overlap layers copies") {
    Fixture a;
    a.play(PlayMode::Restart); a.play(PlayMode::Restart);
    a.render(8);
    // The first copy is fading out, the second starts: after the fade only one remains.
    a.render(300);
    REQUIRE(a.pool.active() == 1);

    Fixture b;
    b.play(PlayMode::Overlap); b.play(PlayMode::Overlap);
    REQUIRE(b.render(8) == 2);
    REQUIRE(b.L[0] == 1.0f);
}

TEST_CASE("hold mode stops on release") {
    Fixture f(48000);
    f.play(PlayMode::Hold);
    f.render();
    REQUIRE(f.pool.isPlaying(3));
    f.pool.apply({Command::Release, PlayMode::Hold, 3, 1.f});
    f.render();
    REQUIRE(f.pool.active() == 0);
}

TEST_CASE("toggle loop keeps playing past the end until pressed again") {
    Fixture f(300);
    f.play(PlayMode::ToggleLoop);
    for (int i = 0; i < 5; ++i) REQUIRE(f.render() == 1);
    f.play(PlayMode::ToggleLoop);
    f.render();
    REQUIRE(f.pool.active() == 0);
}

TEST_CASE("removing a clip from the bank drops its voices without touching it") {
    Fixture f(48000);
    f.play(PlayMode::Overlap);
    f.render();
    f.bank.entries[3].store(nullptr);
    f.clip.reset(); // freed: the pool must not read it any more
    REQUIRE(f.render() == 0);
}

TEST_CASE("empty slots and stop-all are handled") {
    Fixture f(48000);
    f.play(PlayMode::Restart, 7); // nothing loaded in slot 7
    REQUIRE(f.render() == 0);
    for (int i = 0; i < 20; ++i) f.play(PlayMode::Overlap); // more than kMaxVoices
    REQUIRE(f.pool.active() == kMaxVoices);
    f.pool.apply({Command::StopAll});
    f.render();
    REQUIRE(f.pool.active() == 0);
}
