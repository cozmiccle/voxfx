#pragma once
// Maps input events to actions. Thread-safe: backends call handle() from their
// own threads; the dispatcher it calls must be thread-safe too (the app's
// dispatcher only pushes to the engine queue and posts to the UI thread).
#include "common/types.h"
#include "input/input_event.h"

#include <QJsonArray>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace vfx {

enum class Action : uint8_t {
    PlaySound, StopAll, PushToTalk, ToggleMute, ToggleFx, NextPreset, BankNext, BankPrev
};
const char* actionName(Action a);
bool actionFromName(const std::string& s, Action& out);

struct Binding {
    std::string device;      // "*" matches any device
    std::string deviceLabel;
    std::string control;
    std::string controlLabel;
    uint32_t mods = 0;
    int bank = -1;           // -1 = active in every bank
    Action action = Action::PlaySound;
    std::string soundId;     // for PlaySound
    bool useVelocity = true; // MIDI velocity scales clip volume

    std::string describe() const; // "Macro Pad: Ctrl+KEY_F13"
};

class BindingManager {
public:
    using Dispatcher = std::function<void(const Binding&, const InputEvent&)>;
    using LearnCallback = std::function<void(const InputEvent&)>;
    static constexpr int kNumBanks = 8;

    void setDispatcher(Dispatcher d);
    void handle(const InputEvent& ev);

    // The next *press* from any device is delivered to cb instead of dispatched.
    void startLearn(LearnCallback cb);
    void cancelLearn();

    std::vector<Binding> bindings() const;
    void setBindings(std::vector<Binding> b);
    void add(const Binding& b);   // replaces any binding with the same trigger + bank
    void removeAt(size_t index);
    void removeForSound(const std::string& soundId);
    std::vector<Binding> forSound(const std::string& soundId) const;
    // Existing binding that would collide with this trigger, or nullptr-equivalent (empty optional-ish).
    bool findConflict(const Binding& b, Binding* out) const;

    int bank() const;
    void setBank(int b);

    QJsonArray toJson() const;
    void fromJson(const QJsonArray& a);

private:
    static bool sameTrigger(const Binding& a, const Binding& b);
    mutable std::mutex m_;
    std::vector<Binding> bindings_;
    Dispatcher dispatch_;
    LearnCallback learn_;
    int bank_ = 0;
    // Press -> binding, so the matching release goes to the same binding even if
    // modifiers or the bank changed in between.
    std::map<std::string, Binding> held_;
};

} // namespace vfx
