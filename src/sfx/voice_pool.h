#pragma once
// Plays clips from a ClipBank. Lives entirely on the audio thread: commands come
// in through apply(), audio goes out through render(). No allocation, no locks.
//
// Voices started with Command::preview play on a separate bus that the engine
// sends only to the monitor (your headphones), never to the virtual mic.
#include "common/types.h"
#include "sfx/clip.h"

namespace vfx {

class VoicePool {
public:
    explicit VoicePool(const ClipBank& bank) : bank_(bank) {}

    void setOutputRate(float rate) { step_ = double(kClipRate) / rate; }
    void apply(const Command& c);
    // Mixes n samples into the main bus (L/R) and the preview bus (pL/pR).
    // pL/pR may be null, in which case preview voices are rendered silently.
    // Returns the number of active voices.
    int render(float* L, float* R, float* pL, float* pR, int n);
    int render(float* L, float* R, int n) { return render(L, R, nullptr, nullptr, n); }
    bool isPlaying(int slot) const;
    int active() const;

    // Calls f(slot, progress 0..1) for every voice that is playing (not fading out).
    template <typename F>
    void forEachPlaying(F&& f) const {
        for (const auto& v : voices_)
            if (v.clip && v.fadeOut < 0)
                f(v.slot, float(v.pos / double(v.clip->frames())));
    }

private:
    struct Voice {
        const Clip* clip = nullptr;
        int slot = -1;
        double pos = 0;
        float gain = 1.f;
        bool loop = false;
        bool holdMode = false;
        bool preview = false;
        int fadeOut = -1; // samples left in the release fade, -1 = not fading
    };
    void startVoice(int slot, float gain, bool loop, bool hold, bool preview);
    void stopSlot(int slot);

    const ClipBank& bank_;
    Voice voices_[kMaxVoices];
    double step_ = 1.0;
    static constexpr int kFade = 256; // ~5 ms click-free stop
};

} // namespace vfx
