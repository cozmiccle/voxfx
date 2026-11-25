#include "sfx/recorder.h"

#include "engine/engine.h"

#include <sndfile.h>

#include <chrono>
#include <cmath>
#include <vector>

namespace vfx {

Recorder::Recorder(Engine& engine) : engine_(engine) {}
Recorder::~Recorder() { stop(); }

double Recorder::seconds() const { return double(frames_.load()) / rate_; }

bool Recorder::start(const std::string& path, std::string* error) {
    if (running_) { if (error) *error = "already recording"; return false; }
    rate_ = engine_.meters().sampleRate.load();
    if (rate_ == 0) rate_ = 48000;
    SF_INFO info{};
    info.samplerate = int(rate_);
    info.channels = 1;
    info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_24;
    SNDFILE* f = sf_open(path.c_str(), SFM_WRITE, &info);
    if (!f) { if (error) *error = std::string("cannot create file: ") + sf_strerror(nullptr); return false; }
    file_ = f;
    frames_ = 0;
    peak_ = 0;
    hitLimit_ = false;
    stopReq_ = false;
    error_.clear();
    droppedAtStart_ = engine_.captureDropped();
    engine_.captureRing().discard(); // stale audio from a previous take
    engine_.setCapture(true);
    running_ = true;
    thread_ = std::thread([this] { run(); });
    return true;
}

void Recorder::run() {
    std::vector<float> buf(8192);
    const uint64_t maxFrames = uint64_t(kMaxSeconds * rate_);
    auto* f = static_cast<SNDFILE*>(file_);
    for (;;) {
        bool last = stopReq_.load();
        size_t n;
        while ((n = engine_.captureRing().read(buf.data(), buf.size())) > 0) {
            uint64_t room = maxFrames - frames_.load();
            if (n > room) { n = size_t(room); hitLimit_ = true; }
            float pk = peak_.load();
            for (size_t i = 0; i < n; ++i) pk = std::max(pk, std::fabs(buf[i]));
            peak_ = pk;
            sf_writef_float(f, buf.data(), sf_count_t(n));
            frames_ += n;
            if (hitLimit_) break;
        }
        if (hitLimit_) { engine_.setCapture(false); break; }
        if (last) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

RecordingInfo Recorder::stop() {
    RecordingInfo info;
    if (!running_) return info;
    engine_.setCapture(false);
    // Let the block in flight land in the ring, then drain it.
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    stopReq_ = true;
    if (thread_.joinable()) thread_.join();
    sf_write_sync(static_cast<SNDFILE*>(file_));
    sf_close(static_cast<SNDFILE*>(file_));
    file_ = nullptr;
    running_ = false;
    info.ok = frames_ > 0;
    if (!info.ok) info.error = "nothing was recorded (is a microphone connected?)";
    info.seconds = seconds();
    info.peak = peak_;
    info.dropped = engine_.captureDropped() - droppedAtStart_;
    return info;
}

bool trimSilence(const std::string& path, float threshDb, double padMs, std::string* error) {
    SF_INFO info{};
    SNDFILE* f = sf_open(path.c_str(), SFM_READ, &info);
    if (!f) { if (error) *error = "cannot open recording"; return false; }
    const int ch = info.channels;
    std::vector<float> data(size_t(info.frames) * ch);
    sf_count_t got = sf_readf_float(f, data.data(), info.frames);
    sf_close(f);
    if (got <= 0) { if (error) *error = "empty recording"; return false; }
    const size_t frames = size_t(got);

    // Find first/last frame whose short-term (10 ms) peak is above threshold.
    const float thr = std::pow(10.f, threshDb / 20.f);
    const size_t win = std::max<size_t>(1, size_t(info.samplerate / 100));
    auto loud = [&](size_t start) {
        float pk = 0;
        for (size_t i = start; i < std::min(frames, start + win); ++i)
            for (int c = 0; c < ch; ++c) pk = std::max(pk, std::fabs(data[i * ch + c]));
        return pk > thr;
    };
    size_t first = frames, last = 0;
    for (size_t i = 0; i < frames; i += win) if (loud(i)) { first = i; break; }
    if (first == frames) { if (error) *error = "recording is silent"; return false; }
    for (size_t i = (frames - 1) / win * win;; i -= win) {
        if (loud(i)) { last = std::min(frames, i + win); break; }
        if (i == 0) break;
    }
    const size_t pad = size_t(padMs * 0.001 * info.samplerate);
    size_t a = first > pad ? first - pad : 0;
    size_t b = std::min(frames, last + pad);
    if (a == 0 && b == frames) return true; // nothing to cut

    std::vector<float> out(data.begin() + long(a * ch), data.begin() + long(b * ch));
    const size_t n = b - a, fade = std::min(n / 4, size_t(0.005 * info.samplerate));
    for (size_t i = 0; i < fade; ++i) {
        float g = float(i) / float(fade);
        for (int c = 0; c < ch; ++c) { out[i * ch + c] *= g; out[(n - 1 - i) * ch + c] *= g; }
    }
    SF_INFO winfo = info;
    SNDFILE* w = sf_open(path.c_str(), SFM_WRITE, &winfo);
    if (!w) { if (error) *error = "cannot rewrite recording"; return false; }
    sf_writef_float(w, out.data(), sf_count_t(n));
    sf_close(w);
    return true;
}

} // namespace vfx
