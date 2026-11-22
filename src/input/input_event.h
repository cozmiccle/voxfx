#pragma once
#include <cstdint>
#include <string>

namespace vfx {

// Normalised input from any backend (evdev keyboards/macro pads/gamepads, MIDI).
struct InputEvent {
    std::string device;      // stable key, e.g. "evdev:1234:5678:Macro Pad" or "midi:MPD218"
    std::string deviceLabel; // human-readable device name
    std::string control;     // "key:30", "abs:16:-", "note:9:36", "cc:0:20"
    std::string label;       // human-readable control, e.g. "KEY_A", "Note 36 (ch 10)"
    uint32_t mods = 0;       // keyboard modifiers held (see Mod)
    bool pressed = true;
    float value = 1.f;       // 0..1; MIDI velocity, 1 for keys/buttons
};

enum Mod : uint32_t { ModCtrl = 1, ModShift = 2, ModAlt = 4, ModMeta = 8 };

inline std::string modsToString(uint32_t m) {
    std::string s;
    if (m & ModCtrl) s += "Ctrl+";
    if (m & ModShift) s += "Shift+";
    if (m & ModAlt) s += "Alt+";
    if (m & ModMeta) s += "Meta+";
    return s;
}

} // namespace vfx
