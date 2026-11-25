#include "common/spsc_ring.h"
#include "sfx/recorder.h"
#include "sfx/voice_pool.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <memory>
#include <sndfile.h>
#include <thread>
#include <vector>

using namespace vfx;
namespace fs = std::filesystem;

TEST_CASE("ring keeps order, refuses overflow and can discard") {
    SpscRing<float> r(8);
    float in[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    REQUIRE(r.write(in, 10) == 8); // full after 8
    float out[10];
    REQUIRE(r.read(out, 3) == 3);
    REQUIRE(out[2] == 2.f);
    REQUIRE(r.write(in, 2) == 2);  // wraps around
    REQUIRE(r.available() == 7);
    r.discard();
    REQUIRE(r.available() == 0);
}

TEST_CASE("ring passes a long stream between threads intact") {
    SpscRing<float> r(1024);
    constexpr int kN = 200000;
    std::thread producer([&] {
        float v = 0;
        for (int i = 0; i < kN;) {
            float chunk[64];
            int n = std::min(64, kN - i);
            for (int j = 0; j < n; ++j) chunk[j] = v + j;
            size_t w = r.write(chunk, size_t(n));
            i += int(w);
            v += float(w);
            if (w == 0) std::this_thread::yield();
        }
    });
    int got = 0;
    bool ordered = true;
    float buf[100];
    while (got < kN) {
        size_t n = r.read(buf, 100);
        for (size_t j = 0; j < n; ++j) ordered &= buf[j] == float(got + int(j));
        got += int(n);
    }
    producer.join();
    REQUIRE(ordered);
}

namespace {
std::string writeTake(const char* name, int rate, double leadSil, double tone, double tailSil) {
    fs::path p = fs::temp_directory_path() / name;
    SF_INFO info{};
    info.samplerate = rate; info.channels = 1; info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_24;
    SNDFILE* f = sf_open(p.c_str(), SFM_WRITE, &info);
    std::vector<float> v;
    auto noise = [&](double secs) { for (int i = 0; i < int(secs * rate); ++i) v.push_back(0.001f * ((i * 7919) % 13 - 6) / 6.f); };
    noise(leadSil);
    for (int i = 0; i < int(tone * rate); ++i) v.push_back(0.3f * std::sin(2 * M_PI * 300 * i / rate));
    noise(tailSil);
    sf_writef_float(f, v.data(), sf_count_t(v.size()));
    sf_close(f);
    return p.string();
}
double durationOf(const std::string& path) {
    SF_INFO info{};
    SNDFILE* f = sf_open(path.c_str(), SFM_READ, &info);
    sf_close(f);
    return double(info.frames) / info.samplerate;
}
} // namespace

TEST_CASE("silence is trimmed from both ends of a recording") {
    auto p = writeTake("voxfx_take.wav", 48000, 1.5, 1.0, 2.0);
    REQUIRE(trimSilence(p, -45.f, 80.0));
    REQUIRE(durationOf(p) == Catch::Approx(1.0 + 0.16).margin(0.03));
    fs::remove(p);
}

TEST_CASE("an all-silent recording is reported and left alone") {
    auto p = writeTake("voxfx_silent.wav", 48000, 1.0, 0.0, 1.0);
    std::string err;
    REQUIRE_FALSE(trimSilence(p, -45.f, 80.0, &err));
    REQUIRE_FALSE(err.empty());
    REQUIRE(durationOf(p) == Catch::Approx(2.0).margin(0.01));
    fs::remove(p);
}

TEST_CASE("preview voices go only to the preview bus; progress is reported") {
    ClipBank bank;
    auto clip = std::make_unique<Clip>();
    clip->left.assign(1000, 0.5f);
    clip->right.assign(1000, 0.5f);
    bank.entries[2].store(clip.get());
    VoicePool pool(bank);
    pool.setOutputRate(48000.f);

    Command c{Command::Play, PlayMode::Overlap, 2, 1.f};
    c.preview = true;
    pool.apply(c);
    float L[100] = {}, R[100] = {}, pL[100] = {}, pR[100] = {};
    pool.render(L, R, pL, pR, 100);
    REQUIRE(L[10] == 0.f);
    REQUIRE(pL[10] == 0.5f);

    float progress = -1;
    pool.forEachPlaying([&](int slot, float p) { if (slot == 2) progress = p; });
    REQUIRE(progress == Catch::Approx(0.1).margin(0.001));
}
