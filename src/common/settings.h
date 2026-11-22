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
};

void loadSettings(QSettings& s, Params& p, AppSettings& a);
void saveSettings(QSettings& s, const Params& p, const AppSettings& a);

} // namespace vfx
