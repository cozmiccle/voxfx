#pragma once
// Persist engine parameters and device choices with QSettings
// (~/.config/voxfx/voxfx.conf).
#include "common/types.h"

#include <QSettings>
#include <QString>
#include <QStringList>

namespace vfx {

struct AppSettings {
    QString micName;            // "" = follow system default
    QStringList grabbedDevices; // evdev device keys grabbed exclusively
    bool startMinimized = false;
    QString theme = "dark";     // "dark", "light" or "system"
    QString padLayout = "auto"; // "auto", "tiles" or "list"
    int padSize = 170;          // target pad width in px
};

void loadSettings(QSettings& s, Params& p, AppSettings& a);
void saveSettings(QSettings& s, const Params& p, const AppSettings& a);

} // namespace vfx
