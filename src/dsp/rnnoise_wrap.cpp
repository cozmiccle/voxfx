#include "dsp/rnnoise_wrap.h"

#ifdef VOXFX_HAVE_RNNOISE
#include <rnnoise.h>
#endif

namespace vfx::dsp {

Denoiser::Denoiser() : in_(kFrame, 0.f), out_(kFrame, 0.f), dry_(kFrame, 0.f) {
#ifdef VOXFX_HAVE_RNNOISE
    st_ = rnnoise_create(nullptr);
#endif
}

Denoiser::~Denoiser() {
#ifdef VOXFX_HAVE_RNNOISE
    if (st_) rnnoise_destroy(st_);
#endif
}

bool Denoiser::available() {
#ifdef VOXFX_HAVE_RNNOISE
    return true;
#else
    return false;
#endif
}

void Denoiser::prepare(float rate) {
    active_ = st_ != nullptr && rate == 48000.f;
    pos_ = 0;
    std::fill(in_.begin(), in_.end(), 0.f);
    std::fill(out_.begin(), out_.end(), 0.f);
    std::fill(dry_.begin(), dry_.end(), 0.f);
}

float Denoiser::process(float x) {
    if (!active_) return x;
    // Emit the processed sample from the previous frame at this position, then
    // collect the new input. Every 480 samples, run one RNNoise frame.
    float y = out_[pos_] * mix_ + dry_[pos_] * (1.f - mix_);
    in_[pos_] = x * 32768.f; // RNNoise expects 16-bit scale
    dry_[pos_] = x;
    if (++pos_ == kFrame) {
        pos_ = 0;
#ifdef VOXFX_HAVE_RNNOISE
        vad_ = rnnoise_process_frame(st_, out_.data(), in_.data());
#endif
        for (float& s : out_) s *= (1.f / 32768.f);
    }
    return y;
}

} // namespace vfx::dsp
