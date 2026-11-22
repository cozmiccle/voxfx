// Headless check of the PipeWire engine: starts it, optionally plays a clip,
// and prints what it sees. Exit code 0 when the virtual mic and the processing
// node exist and audio cycles are running.
//
//   voxfx-engine-smoke [seconds] [clip.wav|mp3]
// Set VOXFX_SMOKE_TONE=1 to create the test-tone source and use it as the mic.
#include "engine/engine.h"
#include "sfx/clip.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

using namespace vfx;

int main(int argc, char** argv) {
    int seconds = argc > 1 ? atoi(argv[1]) : 3;
    Engine e;
    std::string err;
    if (!e.start(&err)) { fprintf(stderr, "start failed: %s\n", err.c_str()); return 2; }

    if (getenv("VOXFX_SMOKE_TONE")) {
        e.setTestTone(true);
        e.selectMicrophone(Engine::kTestToneName);
    }
    Clip clip;
    if (argc > 2) {
        auto r = loadClip(argv[2], clip);
        if (!r.ok) { fprintf(stderr, "clip: %s\n", r.error.c_str()); return 3; }
        printf("clip: %zu frames, %.1f LUFS, gain %.2f\n", clip.frames(), clip.loudnessLufs, clip.normGain);
        e.bank().slots[0].store(&clip);
    }
    // VOXFX_SMOKE_PLAY_AT=<ms>: when to trigger the clip (default 700 ms).
    const char* at = getenv("VOXFX_SMOKE_PLAY_AT");
    const int playAt = at ? atoi(at) : 700;

    float maxOut = 0, maxSfx = 0, maxIn = 0;
    bool played = argc <= 2;
    for (int i = 0; i < seconds * 10; ++i) {
        if (!played && i * 100 >= playAt) { e.send({Command::Play, PlayMode::Restart, 0, 1.f}); played = true; }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        maxOut = std::max(maxOut, e.meters().out.exchange(0.f));
        maxSfx = std::max(maxSfx, e.meters().sfx.exchange(0.f));
        maxIn = std::max(maxIn, e.meters().micIn.exchange(0.f));
    }
    auto st = e.status();
    printf("connected=%d virtualMic=%d engine=%d links=%d mic='%s'\n", st.connected,
           st.virtualMicReady, st.engineReady, st.links, st.micName.c_str());
    printf("rate=%u cycles=%llu peakMicIn=%.3f peakOut=%.3f peakSfx=%.3f\n", e.meters().sampleRate.load(),
           (unsigned long long)e.meters().cycles.load(), maxIn, maxOut, maxSfx);
    for (auto& m : e.microphones()) printf("mic: %s (%s)\n", m.description.c_str(), m.name.c_str());
    e.bank().slots[0].store(nullptr);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    e.stop();
    return (st.virtualMicReady && st.engineReady && e.meters().cycles.load() > 0) ? 0 : 1;
}
