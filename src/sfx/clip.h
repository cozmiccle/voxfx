#pragma once
#include "common/types.h"
#include <atomic>
#include <string>
#include <vector>

namespace vfx {

// Decoded, resampled (48 kHz stereo) and loudness-analysed sound clip.
// Immutable once published to the audio thread.
struct Clip {
    std::vector<float> left, right;
    float normGain = 1.f;     // brings the clip to the loudness target
    std::atomic<float> trimGain{1.f}; // user trim on top (UI may change it live)
    double loudnessLufs = -70;
    double truePeakDb = -70;
    size_t frames() const { return left.size(); }
};

// Fixed table the audio thread reads. The UI owns the Clip objects; the audio
// thread only ever sees raw pointers. To remove a clip: set the slot to nullptr,
// wait until Meters::cycles has advanced by 2 (every voice checks its slot each
// block and stops if the pointer changed), then free it. See ClipStore in app.cpp.
struct ClipBank {
    std::atomic<const Clip*> entries[kMaxClipSlots] = {};
    const Clip* get(int i) const {
        return (i >= 0 && i < kMaxClipSlots) ? entries[i].load(std::memory_order_acquire) : nullptr;
    }
};

struct LoadResult {
    bool ok = false;
    std::string error;
};

// Decode any libsndfile-supported file (wav/flac/ogg/mp3), downmix/upmix to
// stereo, resample to 48 kHz, and compute the gain that brings it to targetLufs
// without exceeding the true-peak ceiling. Runs on worker threads.
LoadResult loadClip(const std::string& path, Clip& out,
                    double targetLufs = -18.0, double ceilingDbtp = -1.0);

} // namespace vfx
