#include "sfx/voice_pool.h"

namespace vfx {

bool VoicePool::isPlaying(int slot) const {
    for (const auto& v : voices_)
        if (v.clip && v.slot == slot && v.fadeOut < 0) return true;
    return false;
}

int VoicePool::active() const {
    int n = 0;
    for (const auto& v : voices_) n += v.clip != nullptr;
    return n;
}

void VoicePool::stopSlot(int slot) {
    for (auto& v : voices_)
        if (v.clip && v.slot == slot && v.fadeOut < 0) v.fadeOut = kFade;
}

void VoicePool::startVoice(int slot, float gain, bool loop, bool hold, bool preview) {
    const Clip* c = bank_.get(slot);
    if (!c || c->frames() == 0) return;
    // Pick a free voice, otherwise steal the one closest to its end.
    Voice* pick = nullptr;
    for (auto& v : voices_) if (!v.clip) { pick = &v; break; }
    if (!pick) {
        double best = -1;
        for (auto& v : voices_) {
            double progress = v.pos / double(v.clip->frames());
            if (progress > best) { best = progress; pick = &v; }
        }
    }
    *pick = Voice{c, slot, 0.0, gain, loop, hold, preview, -1};
}

void VoicePool::apply(const Command& c) {
    switch (c.type) {
        case Command::Play:
            switch (c.mode) {
                case PlayMode::Restart:
                    stopSlot(c.slot);
                    startVoice(c.slot, c.gain, false, false, c.preview);
                    break;
                case PlayMode::Overlap:
                    startVoice(c.slot, c.gain, false, false, c.preview);
                    break;
                case PlayMode::Hold:
                    stopSlot(c.slot);
                    startVoice(c.slot, c.gain, false, true, c.preview);
                    break;
                case PlayMode::ToggleLoop:
                    if (isPlaying(c.slot)) stopSlot(c.slot);
                    else startVoice(c.slot, c.gain, true, false, c.preview);
                    break;
            }
            break;
        case Command::Release:
            for (auto& v : voices_)
                if (v.clip && v.slot == c.slot && v.holdMode && v.fadeOut < 0) v.fadeOut = kFade;
            break;
        case Command::Stop:
            stopSlot(c.slot);
            break;
        case Command::StopAll:
            for (auto& v : voices_) if (v.clip && v.fadeOut < 0) v.fadeOut = kFade;
            break;
    }
}

int VoicePool::render(float* L, float* R, float* pL, float* pR, int n) {
    int count = 0;
    for (auto& v : voices_) {
        if (!v.clip) continue;
        // The UI removed or replaced this clip: drop the voice immediately and never
        // touch the old pointer again.
        if (bank_.get(v.slot) != v.clip) { v.clip = nullptr; continue; }
        const Clip& c = *v.clip;
        const size_t frames = c.frames();
        const float g = v.gain * c.normGain * c.trimGain.load(std::memory_order_relaxed);
        float* outL = v.preview ? pL : L;
        float* outR = v.preview ? pR : R;
        for (int i = 0; i < n; ++i) {
            size_t i0 = size_t(v.pos);
            if (i0 >= frames) {
                if (v.loop) { v.pos -= double(frames); i0 = size_t(v.pos); }
                else { v.clip = nullptr; break; }
            }
            size_t i1 = i0 + 1 < frames ? i0 + 1 : (v.loop ? 0 : i0);
            float f = float(v.pos - double(i0));
            float fade = 1.f;
            if (v.fadeOut >= 0) {
                if (v.fadeOut == 0) { v.clip = nullptr; break; }
                fade = float(v.fadeOut--) / kFade;
            }
            float l = c.left[i0] + (c.left[i1] - c.left[i0]) * f;
            float r = c.right[i0] + (c.right[i1] - c.right[i0]) * f;
            if (outL) { outL[i] += l * g * fade; outR[i] += r * g * fade; }
            v.pos += step_;
        }
        if (v.clip) ++count;
    }
    return count;
}

} // namespace vfx
