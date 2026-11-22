#pragma once
// RNNoise wrapper. RNNoise works on 480-sample frames at 48 kHz, so this adds a
// fixed 480-sample (10 ms) delay. When RNNoise isn't compiled in, or the graph
// rate isn't 48 kHz, it passes audio through unchanged.
#include <vector>

struct DenoiseState;

namespace vfx::dsp {

class Denoiser {
public:
    static constexpr int kFrame = 480;
    Denoiser();
    ~Denoiser();
    Denoiser(const Denoiser&) = delete;
    Denoiser& operator=(const Denoiser&) = delete;

    static bool available();
    void prepare(float rate);
    void setMix(float m) { mix_ = m; }
    float process(float x);
    float lastVadProbability() const { return vad_; }

private:
    DenoiseState* st_ = nullptr;
    bool active_ = false;
    float mix_ = 1.f, vad_ = 0.f;
    std::vector<float> in_, out_, dry_;
    int pos_ = 0;
};

} // namespace vfx::dsp
