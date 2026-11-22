#pragma once
// PipeWire audio engine.
//
// Graph created at start():
//   [selected mic] --> voxfx_engine (pw_filter, RT thread) --> "voxfx Mic" (virtual source)
//                                                    \------> default sink (monitor, optional)
//
// Links are created by us through link-factory, not by WirePlumber policy, so
// they're predictable. Registry/metadata events keep them up to date when
// devices are plugged in or the default devices change.
#include "common/mpmc_queue.h"
#include "common/types.h"
#include "sfx/clip.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace vfx {

struct AudioDevice {
    uint32_t id = 0;
    std::string name;        // node.name (stable across sessions)
    std::string description; // human readable
};

struct EngineStatus {
    bool connected = false;
    bool virtualMicReady = false;
    bool engineReady = false;
    std::string micName;      // node.name of the mic currently linked ("" = none)
    std::string monitorSink;  // node.name of the sink used for monitoring
    int links = 0;
};

class Engine {
public:
    static constexpr const char* kVirtualMicName = "voxfx_mic";
    static constexpr const char* kVirtualMicDesc = "voxfx Mic";
    static constexpr const char* kTestToneName = "voxfx_test_tone";

    Engine();
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    bool start(std::string* error);
    void stop();

    Params& params();
    Meters& meters();
    ClipBank& bank();

    // Thread-safe from any non-RT thread. Returns false if the queue is full.
    bool send(const Command& c);

    std::vector<AudioDevice> microphones();
    // "" follows the system default input (skipping our own virtual mic).
    void selectMicrophone(const std::string& nodeName);
    std::string selectedMicrophone();
    void setMonitor(bool on);
    // Adds a 440 Hz "voxfx Test Tone" input you can select as the mic, to check
    // routing without speaking (also used by the headless smoke test).
    void setTestTone(bool on);
    EngineStatus status();

    // Called on the PipeWire thread whenever devices or links change.
    void setChangedCallback(std::function<void()> cb);

    struct Impl;
private:
    std::unique_ptr<Impl> d;
};

} // namespace vfx
