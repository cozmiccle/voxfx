#include "common/settings.h"

namespace vfx {

namespace {
template <typename T, typename V>
void get(QSettings& s, const char* key, std::atomic<T>& a) {
    if (s.contains(key)) a.store(T(s.value(key).value<V>()));
}
} // namespace

void loadSettings(QSettings& s, Params& p, AppSettings& a) {
    s.beginGroup("audio");
    get<float, double>(s, "micGainDb", p.micGainDb);
    get<bool, bool>(s, "cleanupOn", p.cleanupOn);
    get<float, double>(s, "nsMix", p.nsMix);
    get<float, double>(s, "gateThreshDb", p.gateThreshDb);
    get<bool, bool>(s, "compOn", p.compOn);
    get<bool, bool>(s, "limiterOn", p.limiterOn);
    get<bool, bool>(s, "fxOn", p.fxOn);
    get<int, int>(s, "preset", p.preset);
    get<float, double>(s, "sfxGainDb", p.sfxGainDb);
    get<float, double>(s, "duckDb", p.duckDb);
    get<bool, bool>(s, "monitorOn", p.monitorOn);
    get<float, double>(s, "monitorGainDb", p.monitorGainDb);
    get<bool, bool>(s, "monitorMic", p.monitorMic);
    get<bool, bool>(s, "pttEnabled", p.pttEnabled);
    s.endGroup();
    a.micName = s.value("devices/mic").toString();
    a.grabbedDevices = s.value("devices/grabbed").toStringList();
    a.startMinimized = s.value("ui/startMinimized", false).toBool();
}

void saveSettings(QSettings& s, const Params& p, const AppSettings& a) {
    s.beginGroup("audio");
    s.setValue("micGainDb", double(p.micGainDb.load()));
    s.setValue("cleanupOn", p.cleanupOn.load());
    s.setValue("nsMix", double(p.nsMix.load()));
    s.setValue("gateThreshDb", double(p.gateThreshDb.load()));
    s.setValue("compOn", p.compOn.load());
    s.setValue("limiterOn", p.limiterOn.load());
    s.setValue("fxOn", p.fxOn.load());
    s.setValue("preset", p.preset.load());
    s.setValue("sfxGainDb", double(p.sfxGainDb.load()));
    s.setValue("duckDb", double(p.duckDb.load()));
    s.setValue("monitorOn", p.monitorOn.load());
    s.setValue("monitorGainDb", double(p.monitorGainDb.load()));
    s.setValue("monitorMic", p.monitorMic.load());
    s.setValue("pttEnabled", p.pttEnabled.load());
    s.endGroup();
    s.setValue("devices/mic", a.micName);
    s.setValue("devices/grabbed", a.grabbedDevices);
    s.setValue("ui/startMinimized", a.startMinimized);
}

} // namespace vfx
