#pragma once
// Shared plain types: commands sent to the audio thread, play modes, parameters
// (atomics the UI writes and the audio thread reads) and meters (the reverse).
#include <atomic>
#include <cmath>
#include <cstdint>

namespace vfx {

constexpr int kMaxClipSlots = 512;   // soundboard slots addressable by the engine
constexpr int kMaxVoices    = 16;    // simultaneously playing clips
constexpr int kClipRate     = 48000; // clips are stored at this rate

enum class PlayMode : uint8_t {
    Restart,   // one-shot; pressing again restarts it
    Overlap,   // one-shot; pressing again layers another copy
    Hold,      // plays while held, stops on release
    ToggleLoop // press starts looping, press again stops
};

enum class VoicePreset : uint8_t { None, Deep, Chipmunk, Robot, Radio, Cave, Count };

inline const char* presetName(VoicePreset p) {
    switch (p) {
        case VoicePreset::None:     return "None";
        case VoicePreset::Deep:     return "Deep";
        case VoicePreset::Chipmunk: return "Chipmunk";
        case VoicePreset::Robot:    return "Robot";
        case VoicePreset::Radio:    return "Radio";
        case VoicePreset::Cave:     return "Cave";
        default:                    return "?";
    }
}

struct Command {
    enum Type : uint8_t { Play, Release, Stop, StopAll } type;
    PlayMode mode = PlayMode::Restart;
    int16_t slot = -1;
    float gain = 1.0f; // linear, e.g. MIDI velocity scaling
};

// Written by the UI thread, read by the RT thread every block. Relaxed atomics are
// enough: each value is independent and a one-block delay is harmless.
struct Params {
    std::atomic<bool>  micMute{false};
    std::atomic<bool>  pttEnabled{false};   // push-to-talk mode on
    std::atomic<bool>  pttHeld{false};
    std::atomic<float> micGainDb{0.f};

    std::atomic<bool>  cleanupOn{true};     // HPF + noise suppression + gate
    std::atomic<float> nsMix{1.0f};         // RNNoise wet amount 0..1
    std::atomic<float> gateThreshDb{-50.f};
    std::atomic<bool>  compOn{true};
    std::atomic<bool>  limiterOn{true};
    std::atomic<bool>  fxOn{true};
    std::atomic<int>   preset{int(VoicePreset::None)};

    std::atomic<float> sfxGainDb{0.f};
    std::atomic<float> duckDb{-9.f};        // mic reduction while SFX play (0 = off)

    std::atomic<bool>  monitorOn{false};
    std::atomic<float> monitorGainDb{-6.f};
    std::atomic<bool>  monitorMic{false};   // false: monitor SFX only (avoids hearing own voice delayed)
};

struct Meters {
    std::atomic<float> micIn{0.f};   // peak, linear
    std::atomic<float> micOut{0.f};
    std::atomic<float> sfx{0.f};
    std::atomic<float> out{0.f};
    std::atomic<bool>  gateOpen{false};
    std::atomic<float> noiseFloorDb{-90.f}; // short-term RMS for gate calibration
    std::atomic<uint32_t> activeVoices{0};
    std::atomic<uint32_t> sampleRate{0};
    std::atomic<uint64_t> cycles{0};        // incremented once per audio block
};

inline float dbToLin(float db) { return std::pow(10.f, db / 20.f); }

} // namespace vfx
