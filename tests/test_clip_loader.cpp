#include "sfx/clip.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <sndfile.h>
#include <vector>

using namespace vfx;
namespace fs = std::filesystem;

namespace {
std::string writeSine(const char* name, int rate, int channels, double seconds, double dbfs) {
    fs::path p = fs::temp_directory_path() / name;
    SF_INFO info{};
    info.samplerate = rate;
    info.channels = channels;
    info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
    SNDFILE* f = sf_open(p.c_str(), SFM_WRITE, &info);
    REQUIRE(f);
    size_t frames = size_t(rate * seconds);
    std::vector<float> buf(frames * channels);
    float amp = float(std::pow(10.0, dbfs / 20.0));
    for (size_t i = 0; i < frames; ++i)
        for (int c = 0; c < channels; ++c) buf[i * channels + c] = amp * std::sin(2 * M_PI * 1000 * i / rate);
    sf_writef_float(f, buf.data(), sf_count_t(frames));
    sf_close(f);
    return p.string();
}
} // namespace

TEST_CASE("clips are resampled to 48 kHz stereo and normalised to the loudness target") {
    auto path = writeSine("voxfx_test_quiet.wav", 44100, 1, 3.0, -35.0);
    Clip c;
    auto r = loadClip(path, c, -18.0, -1.0);
    REQUIRE(r.ok);
    REQUIRE(c.frames() == Catch::Approx(3.0 * 48000).margin(64));
    REQUIRE(c.left.size() == c.right.size());
    double after = c.loudnessLufs + 20.0 * std::log10(c.normGain);
    REQUIRE(after == Catch::Approx(-18.0).margin(0.5));
    fs::remove(path);
}

TEST_CASE("loud clips are limited by the true-peak ceiling") {
    auto path = writeSine("voxfx_test_loud.wav", 48000, 2, 2.0, -0.1);
    Clip c;
    REQUIRE(loadClip(path, c, -10.0, -1.0).ok);
    double peakAfter = c.truePeakDb + 20.0 * std::log10(c.normGain);
    REQUIRE(peakAfter <= -1.0 + 0.01);
    fs::remove(path);
}

TEST_CASE("very short clips fall back to peak normalisation") {
    auto path = writeSine("voxfx_test_short.wav", 48000, 1, 0.15, -30.0);
    Clip c;
    REQUIRE(loadClip(path, c).ok);
    double peakAfter = c.truePeakDb + 20.0 * std::log10(c.normGain);
    REQUIRE(peakAfter == Catch::Approx(-1.0).margin(0.2));
    fs::remove(path);
}

TEST_CASE("missing or garbage files report an error") {
    Clip c;
    REQUIRE_FALSE(loadClip("/nonexistent/file.mp3", c).ok);
    auto p = (fs::temp_directory_path() / "voxfx_garbage.mp3").string();
    FILE* f = fopen(p.c_str(), "wb");
    fputs("this is not audio", f);
    fclose(f);
    REQUIRE_FALSE(loadClip(p, c).ok);
    fs::remove(p);
}
