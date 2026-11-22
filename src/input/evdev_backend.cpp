#include "input/evdev_backend.h"

#include <libevdev/libevdev.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <map>
#include <poll.h>
#include <unistd.h>

namespace vfx {

struct EvdevBackend::Dev {
    int fd = -1;
    libevdev* ev = nullptr;
    InputDeviceInfo info;
    uint32_t mods = 0;
    std::map<int, int> absState;   // code -> -1/0/+1 (hat) or 0/1 (trigger)
    ~Dev() {
        if (ev) libevdev_free(ev);
        if (fd >= 0) close(fd);
    }
};

namespace {

uint32_t modBit(int code) {
    switch (code) {
        case KEY_LEFTCTRL: case KEY_RIGHTCTRL: return ModCtrl;
        case KEY_LEFTSHIFT: case KEY_RIGHTSHIFT: return ModShift;
        case KEY_LEFTALT: case KEY_RIGHTALT: return ModAlt;
        case KEY_LEFTMETA: case KEY_RIGHTMETA: return ModMeta;
        default: return 0;
    }
}

bool ignoredKey(int code) {
    // Touch/tool codes from touchpads and tablets aren't useful triggers.
    return (code >= BTN_DIGI && code <= BTN_TOOL_QUADTAP) || code == BTN_TOUCH;
}

bool isTriggerAxis(int code) {
    return code == ABS_Z || code == ABS_RZ || code == ABS_GAS || code == ABS_BRAKE;
}
bool isHatAxis(int code) { return code >= ABS_HAT0X && code <= ABS_HAT3Y; }

} // namespace

EvdevBackend::EvdevBackend(Sink sink) : sink_(std::move(sink)) {}
EvdevBackend::~EvdevBackend() { stop(); }

void EvdevBackend::start() {
    if (running_.exchange(true)) return;
    thread_ = std::thread([this] { run(); });
}

void EvdevBackend::stop() {
    if (!running_.exchange(false)) return;
    if (thread_.joinable()) thread_.join();
    std::lock_guard lk(m_);
    devs_.clear();
}

std::vector<InputDeviceInfo> EvdevBackend::devices() const {
    std::lock_guard lk(m_);
    std::vector<InputDeviceInfo> v;
    for (auto& d : devs_) v.push_back(d->info);
    return v;
}

void EvdevBackend::setGrabbed(const std::set<std::string>& keys) {
    std::lock_guard lk(m_);
    grabKeys_ = keys;
    grabDirty_ = true;
}

std::string EvdevBackend::permissionProblem() const {
    std::lock_guard lk(m_);
    if (devs_.empty() && deniedCount_ > 0)
        return "Can't read /dev/input (permission denied). Add yourself to the 'input' group "
               "or install the udev rule, then log out and back in.";
    return {};
}

void EvdevBackend::scan() {
    DIR* dir = opendir("/dev/input");
    if (!dir) return;
    int denied = 0;
    std::map<std::string, int> keyCount;
    {
        std::lock_guard lk(m_);
        for (auto& d : devs_) keyCount[d->info.key]++;
    }
    while (dirent* e = readdir(dir)) {
        if (strncmp(e->d_name, "event", 5) != 0) continue;
        std::string path = std::string("/dev/input/") + e->d_name;
        {
            std::lock_guard lk(m_);
            bool open = false;
            for (auto& d : devs_) if (d->info.path == path) { open = true; break; }
            if (open) continue;
        }
        int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) { if (errno == EACCES) ++denied; continue; }
        auto d = std::make_unique<Dev>();
        d->fd = fd;
        if (libevdev_new_from_fd(fd, &d->ev) < 0) continue;
        if (!libevdev_has_event_type(d->ev, EV_KEY)) continue; // nothing bindable
        const char* name = libevdev_get_name(d->ev);
        const char* uniq = libevdev_get_uniq(d->ev);
        char key[512];
        snprintf(key, sizeof key, "evdev:%04x:%04x:%s%s%s", libevdev_get_id_vendor(d->ev),
                 libevdev_get_id_product(d->ev), name ? name : "?", (uniq && *uniq) ? ":" : "",
                 (uniq && *uniq) ? uniq : "");
        std::string k = key;
        // Two identical devices without serial numbers: disambiguate by order.
        if (int n = keyCount[k]++; n > 0) k += "#" + std::to_string(n + 1);
        d->info.key = k;
        d->info.name = name ? name : "?";
        d->info.path = path;
        d->info.gamepad = libevdev_has_event_code(d->ev, EV_KEY, BTN_SOUTH) ||
                          libevdev_has_event_code(d->ev, EV_KEY, BTN_GAMEPAD);
        d->info.keyboard = libevdev_has_event_code(d->ev, EV_KEY, KEY_A) ||
                           libevdev_has_event_code(d->ev, EV_KEY, KEY_KP1) ||
                           libevdev_has_event_code(d->ev, EV_KEY, KEY_F13);
        std::lock_guard lk(m_);
        if (grabKeys_.count(k) && libevdev_grab(d->ev, LIBEVDEV_GRAB) == 0) d->info.grabbed = true;
        devs_.push_back(std::move(d));
    }
    closedir(dir);
    std::lock_guard lk(m_);
    deniedCount_ = denied;
}

void EvdevBackend::emitKey(Dev& d, int code, bool pressed) {
    if (ignoredKey(code)) return;
    if (uint32_t mb = modBit(code)) {
        // Modifiers on keyboards are tracked, not emitted, so "Ctrl+F13" binds as a
        // combo. On gamepads/macro pads without letters a lone modifier is still a key.
        if (d.info.keyboard) {
            if (pressed) d.mods |= mb; else d.mods &= ~mb;
            return;
        }
    }
    InputEvent ev;
    ev.device = d.info.key;
    ev.deviceLabel = d.info.name;
    ev.control = "key:" + std::to_string(code);
    const char* n = libevdev_event_code_get_name(EV_KEY, unsigned(code));
    ev.label = n ? n : ev.control;
    ev.mods = d.info.keyboard ? d.mods : 0;
    ev.pressed = pressed;
    sink_(ev);
}

void EvdevBackend::emitAbs(Dev& d, int code, int value) {
    int state = 0;
    if (isHatAxis(code)) {
        state = value < 0 ? -1 : value > 0 ? 1 : 0;
    } else if (isTriggerAxis(code) && d.info.gamepad) {
        const input_absinfo* ai = libevdev_get_abs_info(d.ev, unsigned(code));
        if (!ai || ai->maximum <= ai->minimum) return;
        float t = float(value - ai->minimum) / float(ai->maximum - ai->minimum);
        int prev = d.absState[code];
        state = prev ? (t > 0.4f) : (t > 0.6f); // hysteresis
    } else {
        return; // sticks etc. are ignored
    }
    int prev = d.absState[code];
    if (state == prev) return;
    d.absState[code] = state;
    const char* n = libevdev_event_code_get_name(EV_ABS, unsigned(code));
    auto send = [&](int dir, bool pressed) {
        InputEvent ev;
        ev.device = d.info.key;
        ev.deviceLabel = d.info.name;
        ev.control = "abs:" + std::to_string(code) + (dir < 0 ? ":-" : ":+");
        ev.label = std::string(n ? n : "ABS") + (dir < 0 ? "-" : "+");
        ev.pressed = pressed;
        sink_(ev);
    };
    if (prev != 0) send(prev, false);
    if (state != 0) send(state, true);
}

void EvdevBackend::readDevice(Dev& d) {
    input_event e;
    int rc;
    while ((rc = libevdev_next_event(d.ev, LIBEVDEV_READ_FLAG_NORMAL, &e)) >= 0) {
        if (rc == LIBEVDEV_READ_STATUS_SYNC) {
            // Kernel dropped events: drain the sync queue and carry on.
            while (libevdev_next_event(d.ev, LIBEVDEV_READ_FLAG_SYNC, &e) == LIBEVDEV_READ_STATUS_SYNC) {}
            continue;
        }
        if (e.type == EV_KEY && e.value != 2) emitKey(d, e.code, e.value == 1);
        else if (e.type == EV_ABS) emitAbs(d, e.code, e.value);
    }
    if (rc != -EAGAIN) {
        // Device unplugged (ENODEV) or broken: close it; the next scan reopens if it returns.
        std::lock_guard lk(m_);
        for (auto it = devs_.begin(); it != devs_.end(); ++it)
            if (it->get() == &d) { devs_.erase(it); break; }
    }
}

void EvdevBackend::run() {
    int sinceScan = 1 << 30;
    while (running_) {
        if (sinceScan >= 2000) { scan(); sinceScan = 0; }

        std::vector<pollfd> pfds;
        std::vector<Dev*> order;
        {
            std::lock_guard lk(m_);
            if (grabDirty_) {
                for (auto& d : devs_) {
                    bool want = grabKeys_.count(d->info.key) > 0;
                    if (want != d->info.grabbed &&
                        libevdev_grab(d->ev, want ? LIBEVDEV_GRAB : LIBEVDEV_UNGRAB) == 0)
                        d->info.grabbed = want;
                }
                grabDirty_ = false;
            }
            for (auto& d : devs_) { pfds.push_back({d->fd, POLLIN, 0}); order.push_back(d.get()); }
        }
        int timeout = 250;
        int n = pfds.empty() ? (usleep(timeout * 1000), 0) : poll(pfds.data(), pfds.size(), timeout);
        sinceScan += timeout; // coarse, good enough for hotplug
        if (n <= 0) continue;
        for (size_t i = 0; i < pfds.size(); ++i)
            if (pfds[i].revents & (POLLIN | POLLERR | POLLHUP)) readDevice(*order[i]);
    }
}

} // namespace vfx
