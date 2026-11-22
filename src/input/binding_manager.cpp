#include "input/binding_manager.h"

#include <QJsonObject>
#include <algorithm>

namespace vfx {

namespace {
const char* kActionNames[] = {"play", "stop_all", "push_to_talk", "toggle_mute",
                              "toggle_fx", "next_preset", "bank_next", "bank_prev"};
}

const char* actionName(Action a) { return kActionNames[int(a)]; }

bool actionFromName(const std::string& s, Action& out) {
    for (int i = 0; i < int(std::size(kActionNames)); ++i)
        if (s == kActionNames[i]) { out = Action(i); return true; }
    return false;
}

std::string Binding::describe() const {
    std::string dev = device == "*" ? "Any device" : (deviceLabel.empty() ? device : deviceLabel);
    return dev + ": " + modsToString(mods) + (controlLabel.empty() ? control : controlLabel);
}

void BindingManager::setDispatcher(Dispatcher d) {
    std::lock_guard lk(m_);
    dispatch_ = std::move(d);
}

bool BindingManager::sameTrigger(const Binding& a, const Binding& b) {
    return a.device == b.device && a.control == b.control && a.mods == b.mods &&
           (a.bank == b.bank || a.bank == -1 || b.bank == -1);
}

void BindingManager::handle(const InputEvent& ev) {
    Binding hit;
    Dispatcher d;
    LearnCallback learnCb;
    bool found = false;
    {
        std::lock_guard lk(m_);
        const std::string heldKey = ev.device + "|" + ev.control;
        if (ev.pressed && learn_) {
            learnCb = std::move(learn_);
            learn_ = nullptr;
        } else if (ev.pressed) {
            // Prefer a binding for the current bank over a global one.
            const Binding* best = nullptr;
            for (const auto& b : bindings_) {
                if ((b.device != "*" && b.device != ev.device) || b.control != ev.control || b.mods != ev.mods)
                    continue;
                if (b.bank == bank_) { best = &b; break; }
                if (b.bank == -1 && !best) best = &b;
            }
            if (best) { hit = *best; found = true; held_[heldKey] = hit; }
        } else {
            auto it = held_.find(heldKey);
            if (it != held_.end()) { hit = it->second; found = true; held_.erase(it); }
        }
        d = dispatch_;
    }
    if (learnCb) { learnCb(ev); return; }
    if (found && d) d(hit, ev);
}

void BindingManager::startLearn(LearnCallback cb) {
    std::lock_guard lk(m_);
    learn_ = std::move(cb);
}

void BindingManager::cancelLearn() {
    std::lock_guard lk(m_);
    learn_ = nullptr;
}

std::vector<Binding> BindingManager::bindings() const {
    std::lock_guard lk(m_);
    return bindings_;
}

void BindingManager::setBindings(std::vector<Binding> b) {
    std::lock_guard lk(m_);
    bindings_ = std::move(b);
}

void BindingManager::add(const Binding& b) {
    std::lock_guard lk(m_);
    // Replace only an exact match (same trigger, same bank). A global and a
    // bank-specific binding on the same key may coexist; the bank one wins.
    bindings_.erase(std::remove_if(bindings_.begin(), bindings_.end(),
                                   [&](const Binding& x) { return sameTrigger(x, b) && x.bank == b.bank; }),
                    bindings_.end());
    bindings_.push_back(b);
}

void BindingManager::removeAt(size_t i) {
    std::lock_guard lk(m_);
    if (i < bindings_.size()) bindings_.erase(bindings_.begin() + long(i));
}

void BindingManager::removeForSound(const std::string& id) {
    std::lock_guard lk(m_);
    bindings_.erase(std::remove_if(bindings_.begin(), bindings_.end(),
                                   [&](const Binding& x) { return x.action == Action::PlaySound && x.soundId == id; }),
                    bindings_.end());
}

std::vector<Binding> BindingManager::forSound(const std::string& id) const {
    std::lock_guard lk(m_);
    std::vector<Binding> out;
    for (auto& b : bindings_) if (b.action == Action::PlaySound && b.soundId == id) out.push_back(b);
    return out;
}

bool BindingManager::findConflict(const Binding& b, Binding* out) const {
    std::lock_guard lk(m_);
    for (auto& x : bindings_)
        if (sameTrigger(x, b)) { if (out) *out = x; return true; }
    return false;
}

int BindingManager::bank() const {
    std::lock_guard lk(m_);
    return bank_;
}

void BindingManager::setBank(int b) {
    std::lock_guard lk(m_);
    bank_ = ((b % kNumBanks) + kNumBanks) % kNumBanks;
}

QJsonArray BindingManager::toJson() const {
    std::lock_guard lk(m_);
    QJsonArray a;
    for (auto& b : bindings_) {
        QJsonObject o;
        o["device"] = QString::fromStdString(b.device);
        o["deviceLabel"] = QString::fromStdString(b.deviceLabel);
        o["control"] = QString::fromStdString(b.control);
        o["controlLabel"] = QString::fromStdString(b.controlLabel);
        o["mods"] = int(b.mods);
        o["bank"] = b.bank;
        o["action"] = actionName(b.action);
        o["sound"] = QString::fromStdString(b.soundId);
        o["velocity"] = b.useVelocity;
        a.append(o);
    }
    return a;
}

void BindingManager::fromJson(const QJsonArray& a) {
    std::vector<Binding> v;
    for (const auto& it : a) {
        QJsonObject o = it.toObject();
        Binding b;
        b.device = o["device"].toString().toStdString();
        b.deviceLabel = o["deviceLabel"].toString().toStdString();
        b.control = o["control"].toString().toStdString();
        b.controlLabel = o["controlLabel"].toString().toStdString();
        b.mods = uint32_t(o["mods"].toInt());
        b.bank = o["bank"].toInt(-1);
        if (!actionFromName(o["action"].toString().toStdString(), b.action)) continue;
        b.soundId = o["sound"].toString().toStdString();
        b.useVelocity = o["velocity"].toBool(true);
        if (b.device.empty() || b.control.empty()) continue;
        v.push_back(std::move(b));
    }
    setBindings(std::move(v));
}

} // namespace vfx
