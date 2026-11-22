#include "dsp/voice_chain.h"

namespace vfx::dsp {

void VoiceChain::prepare(float rate) {
    rate_ = rate;
    hpf_.set(Biquad::Type::HighPass, 80.f, 0.707f, rate);
    hpf_.reset();
    denoise_.prepare(rate);
    gate_.prepare(rate);
    pitch_.prepare(rate);
    robot_.prepare(rate);
    radio_.prepare(rate);
    reverb_.prepare(rate);
    comp_.prepare(rate);
    lastPreset_ = VoicePreset::Count;
}

void VoiceChain::process(float* buf, int n, const ChainSettings& s) {
    if (s.preset != lastPreset_) {
        lastPreset_ = s.preset;
        switch (s.preset) {
            case VoicePreset::Deep:     pitch_.setSemitones(-5.f); break;
            case VoicePreset::Chipmunk: pitch_.setSemitones(+7.f); break;
            case VoicePreset::Robot:    pitch_.setSemitones(-2.f); break;
            default:                    pitch_.setSemitones(0.f);  break;
        }
    }
    denoise_.setMix(s.nsMix);
    gate_.threshDb = s.gateThreshDb;

    for (int i = 0; i < n; ++i) {
        float x = buf[i];
        if (s.cleanup) {
            x = hpf_.process(x);
            x = denoise_.process(x);
            x = gate_.process(x);
        }
        if (s.fx) {
            switch (s.preset) {
                case VoicePreset::Deep:
                case VoicePreset::Chipmunk: x = pitch_.process(x); break;
                case VoicePreset::Robot:    x = robot_.process(pitch_.process(x)); break;
                case VoicePreset::Radio:    x = radio_.process(x); break;
                case VoicePreset::Cave:     x = reverb_.process(x); break;
                default: break;
            }
        }
        if (s.comp) x = comp_.process(x);
        buf[i] = x;
    }
}

} // namespace vfx::dsp
