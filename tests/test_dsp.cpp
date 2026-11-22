#include "dsp/dsp.h"
#include "dsp/voice_chain.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <random>

using namespace vfx;
using namespace vfx::dsp;

namespace {
constexpr float kRate = 48000.f;

std::vector<float> sine(float freq, float amp, int n) {
    std::vector<float> v(n);
    for (int i = 0; i < n; ++i) v[i] = amp * std::sin(2 * kPi * freq * i / kRate);
    return v;
}
float peak(const std::vector<float>& v, int from = 0) {
    float p = 0;
    for (size_t i = from; i < v.size(); ++i) p = std::max(p, std::fabs(v[i]));
    return p;
}
float rms(const std::vector<float>& v, int from = 0) {
    double s = 0;
    for (size_t i = from; i < v.size(); ++i) s += double(v[i]) * v[i];
    return float(std::sqrt(s / double(v.size() - from)));
}
int zeroCrossings(const std::vector<float>& v, int from) {
    int z = 0;
    for (size_t i = from + 1; i < v.size(); ++i) z += (v[i - 1] < 0) != (v[i] < 0);
    return z;
}
} // namespace

TEST_CASE("limiter never exceeds its ceiling") {
    Limiter lim;
    lim.ceilingDb = -1.f;
    lim.prepare(kRate);
    auto x = sine(200, 3.0f, 48000);
    x[10000] = 8.f; x[20001] = -10.f; // isolated spikes
    float ceiling = dbToLin(-1.f);
    for (float& s : x) s = lim.process(s);
    REQUIRE(peak(x) <= ceiling + 1e-6f);
}

TEST_CASE("stereo limiter applies the same gain to both channels") {
    StereoLimiter lim;
    lim.prepare(kRate);
    float l = 0, r = 0;
    for (int i = 0; i < 2000; ++i) { l = 2.f; r = 1.f; lim.process(l, r); }
    REQUIRE(l == Catch::Approx(2.f * r).epsilon(0.01));
    REQUIRE(std::fabs(l) <= dbToLin(-1.f) + 1e-6f);
}

TEST_CASE("high-pass filter removes rumble and keeps voice") {
    Biquad hp;
    hp.set(Biquad::Type::HighPass, 80.f, 0.707f, kRate);
    auto lo = sine(20, 0.5f, 48000), hi = sine(1000, 0.5f, 48000);
    for (float& s : lo) s = hp.process(s);
    hp.reset();
    for (float& s : hi) s = hp.process(s);
    REQUIRE(linToDb(rms(lo, 24000) / (0.5f / std::sqrt(2.f))) < -18.f);
    REQUIRE(linToDb(rms(hi, 24000) / (0.5f / std::sqrt(2.f))) > -0.5f);
}

TEST_CASE("noise gate closes on room noise and opens for speech-level signal") {
    NoiseGate g;
    g.threshDb = -45.f;
    g.prepare(kRate);
    std::mt19937 rng(1);
    std::uniform_real_distribution<float> u(-1, 1);
    std::vector<float> noise(48000);
    for (float& s : noise) s = u(rng) * dbToLin(-65.f);
    float inRms = rms(noise, 24000);
    for (float& s : noise) s = g.process(s);
    REQUIRE(linToDb(rms(noise, 24000) / inRms) < -40.f);
    REQUIRE_FALSE(g.open);

    auto voice = sine(300, dbToLin(-20.f), 24000);
    float vin = rms(voice, 12000);
    for (float& s : voice) s = g.process(s);
    REQUIRE(g.open);
    REQUIRE(rms(voice, 12000) == Catch::Approx(vin).epsilon(0.05));
}

TEST_CASE("compressor reduces loud signals more than quiet ones") {
    auto gainFor = [](float amp) {
        Compressor c;
        c.makeupDb = 0.f;
        c.prepare(kRate);
        auto x = sine(500, amp, 48000);
        float in = rms(x, 24000);
        for (float& s : x) s = c.process(s);
        return linToDb(rms(x, 24000) / in);
    };
    float quiet = gainFor(dbToLin(-40.f));
    float loud = gainFor(dbToLin(-3.f));
    REQUIRE(quiet > -0.5f);  // below threshold: untouched
    REQUIRE(loud < -6.f);    // well above: clearly compressed
}

TEST_CASE("pitch shifter moves pitch up an octave") {
    PitchShifter p;
    p.prepare(kRate);
    p.setSemitones(12.f);
    auto x = sine(220, 0.5f, 48000);
    int inZ = zeroCrossings(x, 9600);
    for (float& s : x) s = p.process(s);
    int outZ = zeroCrossings(x, 9600);
    REQUIRE(double(outZ) / inZ == Catch::Approx(2.0).epsilon(0.1));
}

TEST_CASE("every voice preset produces finite, non-silent audio") {
    VoiceChain chain;
    chain.prepare(kRate);
    for (int p = 0; p < int(VoicePreset::Count); ++p) {
        ChainSettings cs;
        cs.cleanup = false;
        cs.preset = VoicePreset(p);
        auto x = sine(250, 0.3f, 24000);
        for (int off = 0; off < 24000; off += 256) chain.process(x.data() + off, std::min(256, 24000 - off), cs);
        INFO("preset " << presetName(VoicePreset(p)));
        for (float s : x) REQUIRE(std::isfinite(s));
        REQUIRE(rms(x, 12000) > 0.01f);
    }
}
