#pragma once
// App-wide colour theme. "dark" (default) and "light" use Qt's Fusion style with
// our own palettes so the app looks the same on every desktop; "system" leaves
// the desktop's style and palette alone.
#include <QColor>
#include <QString>

namespace vfx::theme {

void apply(const QString& name); // "dark", "light" or "system"
QString current();
bool isDark();                   // true when the active palette is dark

// Status colours that stay readable on the active background.
QColor ok();
QColor warn();
QColor error();
QString css(const QColor& c);    // "#rrggbb" for rich text / style sheets

} // namespace vfx::theme
