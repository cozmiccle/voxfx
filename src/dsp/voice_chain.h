#pragma once
// The mic processing chain:
//   HPF -> denoise -> gate -> voice preset -> compressor
// The limiter lives in the engine after the SFX mix.
#include "common/types.h"
#include "dsp/dsp.h"
#include "dsp/rnnoise_wrap.h"

namespace vfx::dsp {

struct ChainSettings {
    bool cleanup = true;
    float nsMix = 1.f;
    float gateThreshDb = -50.f;
    bool comp = true;
    bool fx = true;
    VoicePreset preset = VoicePreset::None;
};

class VoiceChain {
public:
    void prepare(float rate);
    // In-place, n samples of mono audio. Allocation-free.
    void process(float* buf, int n, const ChainSettings& s);
    bool gateOpen() const { return gate_.open; }
    float rate() const { return rate_; }

private:
    float rate_ = 48000.f;
    VoicePreset lastPreset_ = VoicePreset::Count;
    Biquad hpf_;
    Denoiser denoise_;
    NoiseGate gate_;
    PitchShifter pitch_;
    RingMod robot_;
    Radio radio_;
    Reverb reverb_;
    Compressor comp_;
};

} // namespace vfx::dsp
