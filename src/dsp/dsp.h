#pragma once
// Small, allocation-free DSP blocks. Everything here is header-only, has no
// PipeWire dependency and processes mono float samples, so it can be unit-tested
// offline. Buffers are sized for up to 192 kHz at construction time.
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "common/types.h" // dbToLin

namespace vfx::dsp {

constexpr float kPi = 3.14159265358979f;

inline float linToDb(float x) { return 20.f * std::log10(std::max(x, 1e-9f)); }
inline float coefFromMs(float ms, float rate) {
    return ms <= 0.f ? 0.f : std::exp(-1.f / (ms * 0.001f * rate));
}

// ---------------------------------------------------------------- Biquad (RBJ)
struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    enum class Type { LowPass, HighPass, BandPass };

    void set(Type t, float freq, float q, float rate) {
        float w0 = 2.f * kPi * std::min(freq, rate * 0.45f) / rate;
        float c = std::cos(w0), s = std::sin(w0), alpha = s / (2.f * q);
        float a0 = 1.f + alpha;
        switch (t) {
            case Type::LowPass:  b0 = (1 - c) / 2; b1 = 1 - c;    b2 = (1 - c) / 2; break;
            case Type::HighPass: b0 = (1 + c) / 2; b1 = -(1 + c); b2 = (1 + c) / 2; break;
            case Type::BandPass: b0 = alpha;       b1 = 0;        b2 = -alpha;      break;
        }
        a1 = -2 * c; a2 = 1 - alpha;
        b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0;
    }
    float process(float x) { // transposed direct form II
        float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() { z1 = z2 = 0; }
};

// ---------------------------------------------------------------- Noise gate
// Opens above threshold, closes 4 dB below it (hysteresis), with hold time and a
// smooth gain ramp so it doesn't chatter or click.
struct NoiseGate {
    float threshDb = -50.f, hystDb = 4.f, rangeDb = -60.f;
    float holdMs = 120.f;
    float env = 0, gain = 0, rate = 48000;
    float envAtt = 0, envRel = 0, gainAtt = 0, gainRel = 0;
    int holdSamples = 0, holdLeft = 0;
    bool open = false;

    void prepare(float r) {
        rate = r;
        envAtt = coefFromMs(0.5f, r); envRel = coefFromMs(40.f, r);
        gainAtt = coefFromMs(1.f, r); gainRel = coefFromMs(120.f, r);
        holdSamples = int(holdMs * 0.001f * r);
    }
    float process(float x) {
        float a = std::fabs(x);
        env = a > env ? envAtt * env + (1 - envAtt) * a : envRel * env + (1 - envRel) * a;
        float envDb = linToDb(env);
        if (envDb > threshDb) { open = true; holdLeft = holdSamples; }
        else if (envDb < threshDb - hystDb) {
            if (holdLeft > 0) --holdLeft; else open = false;
        }
        float target = open ? 1.f : dbToLin(rangeDb);
        float c = target > gain ? gainAtt : gainRel;
        gain = c * gain + (1 - c) * target;
        return x * gain;
    }
};

// ---------------------------------------------------------------- Compressor
// Feed-forward, soft-knee, RMS-ish detector in the log domain. Defaults are a
// gentle "voice" setting: -20 dB threshold, 3:1, +6 dB makeup.
struct Compressor {
    float threshDb = -20.f, ratio = 3.f, kneeDb = 6.f, makeupDb = 6.f;
    float attMs = 5.f, relMs = 120.f;
    float env = 0, att = 0, rel = 0, gr = 0; // gr = current gain reduction in dB (<=0)
    float rmsCoef = 0, ms = 0;

    void prepare(float rate) {
        att = coefFromMs(attMs, rate); rel = coefFromMs(relMs, rate);
        rmsCoef = coefFromMs(10.f, rate);
    }
    float computeGainDb(float inDb) const {
        float over = inDb - threshDb;
        if (2 * over < -kneeDb) return 0.f;
        if (2 * std::fabs(over) <= kneeDb) {
            float t = over + kneeDb / 2;
            return (1.f / ratio - 1.f) * t * t / (2 * kneeDb);
        }
        return (1.f / ratio - 1.f) * over;
    }
    float process(float x) {
        ms = rmsCoef * ms + (1 - rmsCoef) * x * x;
        float target = computeGainDb(10.f * std::log10(std::max(ms, 1e-18f)));
        float c = target < gr ? att : rel;
        gr = c * gr + (1 - c) * target;
        return x * dbToLin(gr + makeupDb);
    }
};

// ---------------------------------------------------------------- Limiter
// Lookahead peak limiter. The gain applied to a delayed sample is the minimum
// required gain over the whole lookahead window, so the attack is never late and
// the output never exceeds the ceiling (followed by a hard safety clamp).
struct Limiter {
    float ceilingDb = -1.f, releaseMs = 80.f, lookaheadMs = 1.5f;
    std::vector<float> delay, need;
    int len = 1, pos = 0;
    float g = 1.f, relCoef = 0, ceiling = 1.f;

    void prepare(float rate) {
        len = std::max(1, int(lookaheadMs * 0.001f * rate));
        delay.assign(len, 0.f);
        need.assign(len, 1.f);
        pos = 0; g = 1.f;
        relCoef = coefFromMs(releaseMs, rate);
        ceiling = dbToLin(ceilingDb);
    }
    float process(float x) {
        float a = std::fabs(x);
        need[pos] = a > ceiling ? ceiling / a : 1.f;
        float out = delay[pos];
        delay[pos] = x;
        pos = (pos + 1) % len;
        float target = 1.f;
        for (int i = 0; i < len; ++i) target = std::min(target, need[i]);
        g = target < g ? target : relCoef * g + (1 - relCoef) * target;
        float y = out * g;
        return std::clamp(y, -ceiling, ceiling);
    }
    int latency() const { return len; }
};

// Stereo-linked version of the limiter (same gain on both channels so the
// stereo image doesn't wobble).
struct StereoLimiter {
    float ceilingDb = -1.f, releaseMs = 80.f, lookaheadMs = 1.5f;
    std::vector<float> dl, dr, need;
    int len = 1, pos = 0;
    float g = 1.f, relCoef = 0, ceiling = 1.f;

    void prepare(float rate) {
        len = std::max(1, int(lookaheadMs * 0.001f * rate));
        dl.assign(len, 0.f); dr.assign(len, 0.f); need.assign(len, 1.f);
        pos = 0; g = 1.f;
        relCoef = coefFromMs(releaseMs, rate);
        ceiling = dbToLin(ceilingDb);
    }
    void process(float& l, float& r) {
        float a = std::max(std::fabs(l), std::fabs(r));
        need[pos] = a > ceiling ? ceiling / a : 1.f;
        float ol = dl[pos], orr = dr[pos];
        dl[pos] = l; dr[pos] = r;
        pos = (pos + 1) % len;
        float target = 1.f;
        for (int i = 0; i < len; ++i) target = std::min(target, need[i]);
        g = target < g ? target : relCoef * g + (1 - relCoef) * target;
        l = std::clamp(ol * g, -ceiling, ceiling);
        r = std::clamp(orr * g, -ceiling, ceiling);
    }
};

// ---------------------------------------------------------------- Pitch shifter
// Classic two-tap delay-line shifter: two read heads sweep through a ~40 ms window
// at (1 - ratio) speed, crossfaded with a sine window. Cheap, low latency and
// "voice-changer sounding", which is what we want here.
struct PitchShifter {
    std::vector<float> buf;
    int size = 0, w = 0;
    float phase = 0, ratio = 1, win = 0;

    void prepare(float rate) {
        win = 0.040f * rate;
        size = 1;
        while (size < int(win) * 2 + 4) size <<= 1;
        buf.assign(size, 0.f);
        w = 0; phase = 0;
    }
    void setSemitones(float st) { ratio = std::pow(2.f, st / 12.f); }
    float read(float d) const {
        float rp = float(w) - d;
        while (rp < 0) rp += size;
        int i0 = int(rp) & (size - 1), i1 = (i0 + 1) & (size - 1);
        float f = rp - std::floor(rp);
        return buf[i0] + (buf[i1] - buf[i0]) * f;
    }
    float process(float x) {
        buf[w] = x;
        phase += (1.f - ratio) / win;
        phase -= std::floor(phase);
        float p2 = phase + 0.5f; p2 -= std::floor(p2);
        float y = read(phase * win + 1) * std::sin(kPi * phase) +
                  read(p2 * win + 1) * std::sin(kPi * p2);
        w = (w + 1) & (size - 1);
        return y;
    }
};

// ---------------------------------------------------------------- Ring mod (robot)
struct RingMod {
    float freq = 55.f, ph = 0, inc = 0;
    std::vector<float> comb; int cpos = 0;
    void prepare(float rate) {
        inc = freq / rate;
        comb.assign(std::max(1, int(0.008f * rate)), 0.f); cpos = 0;
    }
    float process(float x) {
        float m = x * std::sin(2 * kPi * ph);
        ph += inc; ph -= std::floor(ph);
        float y = m + 0.55f * comb[cpos];        // short metallic feedback comb
        comb[cpos] = y; cpos = (cpos + 1) % int(comb.size());
        return y * 0.8f;
    }
};

// ---------------------------------------------------------------- Radio
struct Radio {
    Biquad hp, lp;
    float drive = 3.f;
    void prepare(float rate) {
        hp.set(Biquad::Type::HighPass, 400.f, 0.8f, rate);
        lp.set(Biquad::Type::LowPass, 2800.f, 0.8f, rate);
    }
    float process(float x) {
        float y = lp.process(hp.process(x));
        return std::tanh(y * drive) / std::tanh(drive) * 0.9f;
    }
};

// ---------------------------------------------------------------- Small reverb
// Freeverb-style: 4 damped combs in parallel into 2 allpasses.
struct Reverb {
    struct Comb { std::vector<float> b; int p = 0; float store = 0; };
    struct Allpass { std::vector<float> b; int p = 0; };
    std::array<Comb, 4> combs; std::array<Allpass, 2> aps;
    float feedback = 0.82f, damp = 0.3f, wet = 0.35f;

    void prepare(float rate) {
        const int cl[4] = {1116, 1188, 1277, 1356};
        const int al[2] = {556, 441};
        float s = rate / 44100.f;
        for (int i = 0; i < 4; ++i) { combs[i].b.assign(int(cl[i] * s), 0.f); combs[i].p = 0; }
        for (int i = 0; i < 2; ++i) { aps[i].b.assign(int(al[i] * s), 0.f); aps[i].p = 0; }
    }
    float process(float x) {
        float in = x * 0.015f, acc = 0;
        for (auto& c : combs) {
            float o = c.b[c.p];
            c.store = o * (1 - damp) + c.store * damp;
            c.b[c.p] = in + c.store * feedback;
            c.p = (c.p + 1) % int(c.b.size());
            acc += o;
        }
        for (auto& a : aps) {
            float bo = a.b[a.p];
            float o = -acc + bo;
            a.b[a.p] = acc + bo * 0.5f;
            a.p = (a.p + 1) % int(a.b.size());
            acc = o;
        }
        return x * (1 - wet * 0.5f) + acc * wet * 3.f;
    }
};

// ---------------------------------------------------------------- Envelope (ducker)
struct EnvFollower {
    float env = 0, att = 0, rel = 0;
    void prepare(float rate, float attMs = 5.f, float relMs = 250.f) {
        att = coefFromMs(attMs, rate); rel = coefFromMs(relMs, rate);
    }
    float process(float x) {
        float a = std::fabs(x);
        float c = a > env ? att : rel;
        env = c * env + (1 - c) * a;
        return env;
    }
};

} // namespace vfx::dsp
