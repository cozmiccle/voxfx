#pragma once
// Records the engine's processed voice (cleanup + voice effect, no soundboard)
// into a WAV file. The audio thread only writes into Engine::captureRing(); a
// worker thread here drains it to disk, so recording never touches the RT path.
#include <atomic>
#include <string>
#include <thread>

namespace vfx {

class Engine;

struct RecordingInfo {
    bool ok = false;
    std::string error;
    double seconds = 0;
    float peak = 0;          // linear
    uint64_t dropped = 0;    // samples lost (disk too slow): should be 0
};

class Recorder {
public:
    static constexpr double kMaxSeconds = 60.0;

    explicit Recorder(Engine& engine);
    ~Recorder();

    bool start(const std::string& path, std::string* error);
    RecordingInfo stop();            // finishes the file; safe to call when idle
    bool active() const { return running_.load(); }
    double seconds() const;          // recorded so far
    bool hitLimit() const { return hitLimit_.load(); }

private:
    void run();

    Engine& engine_;
    std::thread thread_;
    std::atomic<bool> running_{false}, stopReq_{false}, hitLimit_{false};
    std::atomic<uint64_t> frames_{0};
    std::atomic<float> peak_{0};
    uint32_t rate_ = 48000;
    uint64_t droppedAtStart_ = 0;
    void* file_ = nullptr; // SNDFILE*
    std::string error_;
};

// Cuts silence (below threshDb) from both ends of a WAV file in place, keeping
// padMs of lead-in/out with a short fade. Returns false if the file is all
// silence or can't be processed (the file is left untouched then).
bool trimSilence(const std::string& path, float threshDb = -45.f, double padMs = 80.0,
                 std::string* error = nullptr);

} // namespace vfx
