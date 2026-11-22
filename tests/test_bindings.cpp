#include "input/binding_manager.h"

#include <catch2/catch_test_macros.hpp>

using namespace vfx;

namespace {
InputEvent ev(const std::string& dev, const std::string& ctl, bool pressed = true, uint32_t mods = 0) {
    InputEvent e;
    e.device = dev; e.control = ctl; e.pressed = pressed; e.mods = mods;
    return e;
}
Binding play(const std::string& dev, const std::string& ctl, const std::string& sound, int bank = -1,
             uint32_t mods = 0) {
    Binding b;
    b.device = dev; b.control = ctl; b.soundId = sound; b.bank = bank; b.mods = mods;
    return b;
}
struct Recorder {
    std::vector<std::pair<std::string, bool>> calls;
    BindingManager::Dispatcher fn() {
        return [this](const Binding& b, const InputEvent& e) { calls.push_back({b.soundId, e.pressed}); };
    }
};
} // namespace

TEST_CASE("events trigger the binding for that device and control only") {
    BindingManager m; Recorder r; m.setDispatcher(r.fn());
    m.add(play("midi:Pad", "note:9:36", "kick"));
    m.handle(ev("midi:Pad", "note:9:37"));
    m.handle(ev("midi:Other", "note:9:36"));
    REQUIRE(r.calls.empty());
    m.handle(ev("midi:Pad", "note:9:36"));
    REQUIRE(r.calls.size() == 1);
    REQUIRE(r.calls[0].first == "kick");
}

TEST_CASE("modifiers must match exactly; wildcard device matches anything") {
    BindingManager m; Recorder r; m.setDispatcher(r.fn());
    m.add(play("*", "key:183", "airhorn", -1, ModCtrl));
    m.handle(ev("evdev:kbd", "key:183"));
    REQUIRE(r.calls.empty());
    m.handle(ev("evdev:kbd", "key:183", true, ModCtrl));
    REQUIRE(r.calls.size() == 1);
}

TEST_CASE("bank-specific bindings win over global ones") {
    BindingManager m; Recorder r; m.setDispatcher(r.fn());
    m.add(play("pad", "key:2", "global"));
    m.add(play("pad", "key:2", "bank1", 1));
    m.handle(ev("pad", "key:2"));
    m.setBank(1);
    m.handle(ev("pad", "key:2"));
    REQUIRE(r.calls.size() == 2);
    REQUIRE(r.calls[0].first == "global");
    REQUIRE(r.calls[1].first == "bank1");
}

TEST_CASE("release goes to the binding that handled the press") {
    BindingManager m; Recorder r; m.setDispatcher(r.fn());
    m.add(play("pad", "key:2", "a", 0));
    m.add(play("pad", "key:2", "b", 1));
    m.handle(ev("pad", "key:2", true));
    m.setBank(1);                        // bank switched while held
    m.handle(ev("pad", "key:2", false));
    REQUIRE(r.calls.size() == 2);
    REQUIRE(r.calls[1] == std::make_pair(std::string("a"), false));
}

TEST_CASE("learn mode captures the next press instead of dispatching it") {
    BindingManager m; Recorder r; m.setDispatcher(r.fn());
    m.add(play("pad", "key:2", "a"));
    InputEvent learned;
    m.startLearn([&](const InputEvent& e) { learned = e; });
    m.handle(ev("pad", "key:2"));
    REQUIRE(learned.control == "key:2");
    REQUIRE(r.calls.empty());
    m.handle(ev("pad", "key:2"));
    REQUIRE(r.calls.size() == 1);
}

TEST_CASE("adding a binding replaces the old one on the same trigger; conflicts are reported") {
    BindingManager m;
    m.add(play("pad", "key:2", "a"));
    Binding other;
    REQUIRE(m.findConflict(play("pad", "key:2", "b"), &other));
    REQUIRE(other.soundId == "a");
    m.add(play("pad", "key:2", "b"));
    REQUIRE(m.bindings().size() == 1);
    REQUIRE(m.bindings()[0].soundId == "b");
}

TEST_CASE("bindings survive a JSON round trip") {
    BindingManager a;
    Binding b = play("midi:Pad", "note:9:36", "kick", 2, ModShift);
    b.action = Action::PushToTalk;
    a.add(b);
    BindingManager c;
    c.fromJson(a.toJson());
    REQUIRE(c.bindings().size() == 1);
    auto x = c.bindings()[0];
    REQUIRE(x.device == "midi:Pad");
    REQUIRE(x.control == "note:9:36");
    REQUIRE(x.bank == 2);
    REQUIRE(x.mods == ModShift);
    REQUIRE(x.action == Action::PushToTalk);
}
