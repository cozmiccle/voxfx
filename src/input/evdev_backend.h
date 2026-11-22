#pragma once
// Raw input through /dev/input/event* (libevdev). Covers USB macro pads, extra
// keyboards/numpads, mouse side buttons and gamepads, on any compositor.
//
// Needs read access to /dev/input/event*: add the user to the `input` group, or
// install the udev rule from packaging/ (README explains). Devices can be
// "grabbed" so their key presses only reach voxfx and never type into other apps.
#include "input/input_event.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace vfx {

struct InputDeviceInfo {
    std::string key, name, path;
    bool gamepad = false, keyboard = false, grabbed = false;
};

class EvdevBackend {
public:
    using Sink = std::function<void(const InputEvent&)>;
    explicit EvdevBackend(Sink sink);
    ~EvdevBackend();

    void start();
    void stop();
    std::vector<InputDeviceInfo> devices() const;
    void setGrabbed(const std::set<std::string>& keys); // device keys to grab exclusively
    // Non-empty when no input device could be opened because of permissions.
    std::string permissionProblem() const;

private:
    struct Dev;
    void run();
    void scan();
    void readDevice(Dev& d);
    void emitKey(Dev& d, int code, bool pressed);
    void emitAbs(Dev& d, int code, int value);

    Sink sink_;
    std::atomic<bool> running_{false};
    std::thread thread_;
    mutable std::mutex m_;
    std::vector<std::unique_ptr<Dev>> devs_;
    std::set<std::string> grabKeys_;
    bool grabDirty_ = false;
    int deniedCount_ = 0;
};

} // namespace vfx
