#include "sfx/clip.h"

#include <algorithm>
#include <cmath>
#include <ebur128.h>
#include <sndfile.h>
#include <soxr.h>

namespace vfx {

namespace {
constexpr size_t kMaxSeconds = 120; // refuse absurdly long "sound effects"
}

LoadResult loadClip(const std::string& path, Clip& out, double targetLufs, double ceilingDbtp) {
    LoadResult r;
    SF_INFO info{};
    SNDFILE* f = sf_open(path.c_str(), SFM_READ, &info);
    if (!f) { r.error = std::string("cannot decode: ") + sf_strerror(nullptr); return r; }
    if (info.channels < 1 || info.samplerate < 1000) { sf_close(f); r.error = "bad format"; return r; }
    if (info.frames > sf_count_t(kMaxSeconds) * info.samplerate) {
        sf_close(f); r.error = "clip longer than 2 minutes"; return r;
    }

    // Read interleaved. Some mp3s report frames as unknown, so read in chunks.
    std::vector<float> inter;
    std::vector<float> chunk(4096 * info.channels);
    sf_count_t got;
    while ((got = sf_readf_float(f, chunk.data(), 4096)) > 0) {
        inter.insert(inter.end(), chunk.begin(), chunk.begin() + got * info.channels);
        if (inter.size() > kMaxSeconds * size_t(info.samplerate) * info.channels) break;
    }
    sf_close(f);
    size_t frames = inter.size() / info.channels;
    if (frames == 0) { r.error = "empty file"; return r; }

    // To stereo interleaved.
    std::vector<float> st(frames * 2);
    for (size_t i = 0; i < frames; ++i) {
        const float* s = &inter[i * info.channels];
        float l = s[0], rr = info.channels > 1 ? s[1] : s[0];
        st[2 * i] = l; st[2 * i + 1] = rr;
    }

    // Resample to 48 kHz.
    if (info.samplerate != kClipRate) {
        double ratio = double(kClipRate) / info.samplerate;
        size_t outFrames = size_t(std::ceil(frames * ratio)) + 16;
        std::vector<float> rs(outFrames * 2);
        size_t idone = 0, odone = 0;
        soxr_quality_spec_t q = soxr_quality_spec(SOXR_HQ, 0);
        soxr_error_t e = soxr_oneshot(info.samplerate, kClipRate, 2, st.data(), frames, &idone,
                                      rs.data(), outFrames, &odone, nullptr, &q, nullptr);
        if (e) { r.error = std::string("resample failed: ") + e; return r; }
        rs.resize(odone * 2);
        st.swap(rs);
        frames = odone;
    }

    // Loudness + true peak.
    ebur128_state* eb = ebur128_init(2, kClipRate, EBUR128_MODE_I | EBUR128_MODE_TRUE_PEAK);
    ebur128_add_frames_float(eb, st.data(), frames);
    double lufs = -70;
    if (ebur128_loudness_global(eb, &lufs) != EBUR128_SUCCESS || !std::isfinite(lufs)) lufs = -70;
    double tp0 = 0, tp1 = 0;
    ebur128_true_peak(eb, 0, &tp0);
    ebur128_true_peak(eb, 1, &tp1);
    ebur128_destroy(&eb);
    double tp = std::max(tp0, tp1);
    double tpDb = 20.0 * std::log10(std::max(tp, 1e-9));

    // Very short clips (< 400 ms) have no gated integrated loudness; fall back to
    // peak normalisation for those.
    double gainDb;
    if (lufs <= -69.0) gainDb = ceilingDbtp - tpDb;
    else gainDb = std::min(targetLufs - lufs, ceilingDbtp - tpDb);
    gainDb = std::clamp(gainDb, -40.0, 30.0);

    out.left.resize(frames); out.right.resize(frames);
    for (size_t i = 0; i < frames; ++i) { out.left[i] = st[2 * i]; out.right[i] = st[2 * i + 1]; }
    out.loudnessLufs = lufs;
    out.truePeakDb = tpDb;
    out.normGain = float(std::pow(10.0, gainDb / 20.0));
    r.ok = true;
    return r;
}

} // namespace vfx
