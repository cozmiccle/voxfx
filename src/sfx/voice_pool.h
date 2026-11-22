#pragma once
// Plays clips from a ClipBank. Lives entirely on the audio thread: commands come
// in through apply(), audio goes out through render(). No allocation, no locks.
#include "common/types.h"
#include "sfx/clip.h"

namespace vfx {

class VoicePool {
public:
    explicit VoicePool(const ClipBank& bank) : bank_(bank) {}

    void setOutputRate(float rate) { step_ = double(kClipRate) / rate; }
    void apply(const Command& c);
    // Adds (mixes) n stereo samples into L/R. Returns the number of active voices.
    int render(float* L, float* R, int n);
    bool isPlaying(int slot) const;
    int active() const;

private:
    struct Voice {
        const Clip* clip = nullptr;
        int slot = -1;
        double pos = 0;
        float gain = 1.f;
        bool loop = false;
        bool holdMode = false;
        int fadeOut = -1; // samples left in the release fade, -1 = not fading
    };
    void startVoice(int slot, float gain, bool loop, bool hold);
    void stopSlot(int slot);

    const ClipBank& bank_;
    Voice voices_[kMaxVoices];
    double step_ = 1.0;
    static constexpr int kFade = 256; // ~5 ms click-free stop
};

} // namespace vfx
