#include "ui/theme.h"

#include <QApplication>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>

namespace vfx::theme {

namespace {
QString g_current = "system";
QString g_systemStyle;
QPalette g_systemPalette;
bool g_captured = false;

QPalette darkPalette() {
    const QColor window(0x1e, 0x1f, 0x22), base(0x2a, 0x2c, 0x31), alt(0x31, 0x33, 0x39);
    const QColor button(0x33, 0x35, 0x3b), text(0xe4, 0xe5, 0xe8), dim(0x6c, 0x6f, 0x76);
    const QColor accent(0x4c, 0x8d, 0xff);
    QPalette p;
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, alt);
    p.setColor(QPalette::ToolTipBase, button);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::PlaceholderText, QColor(0x8a, 0x8d, 0x93));
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, button);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, QColor(0xff, 0x6b, 0x6b));
    p.setColor(QPalette::Light, QColor(0x3a, 0x3d, 0x42));
    p.setColor(QPalette::Midlight, QColor(0x33, 0x35, 0x3a));
    p.setColor(QPalette::Mid, QColor(0x26, 0x28, 0x2c));
    p.setColor(QPalette::Dark, QColor(0x12, 0x13, 0x15));
    p.setColor(QPalette::Shadow, QColor(0x0a, 0x0a, 0x0b));
    p.setColor(QPalette::Highlight, accent);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Link, QColor(0x6e, 0xa8, 0xff));
    p.setColor(QPalette::LinkVisited, QColor(0xa7, 0x8b, 0xfa));
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, dim);
    p.setColor(QPalette::Disabled, QPalette::Button, QColor(0x24, 0x26, 0x29));
    p.setColor(QPalette::Disabled, QPalette::Highlight, QColor(0x3a, 0x3d, 0x42));
    p.setColor(QPalette::Disabled, QPalette::HighlightedText, dim);
    return p;
}
} // namespace

void apply(const QString& name) {
    if (!g_captured) { // remember the desktop's look so "system" can restore it
        g_systemStyle = QApplication::style()->name();
        g_systemPalette = QApplication::palette();
        g_captured = true;
    }
    if (name == "system") {
        QApplication::setStyle(QStyleFactory::create(g_systemStyle));
        QApplication::setPalette(g_systemPalette);
        g_current = "system";
        return;
    }
    QApplication::setStyle(QStyleFactory::create("Fusion"));
    if (name == "light") {
        QApplication::setPalette(QApplication::style()->standardPalette());
        g_current = "light";
    } else {
        QApplication::setPalette(darkPalette());
        g_current = "dark";
    }
}

QString current() { return g_current; }

bool isDark() { return QApplication::palette().color(QPalette::Window).lightness() < 128; }

QColor ok() { return isDark() ? QColor(0x4c, 0xc2, 0x7a) : QColor(0x2e, 0x8b, 0x50); }
QColor warn() { return isDark() ? QColor(0xf0, 0xa5, 0x4a) : QColor(0xb8, 0x6e, 0x10); }
QColor error() { return isDark() ? QColor(0xff, 0x6b, 0x6b) : QColor(0xc0, 0x39, 0x2b); }
QString css(const QColor& c) { return c.name(QColor::HexRgb); }

} // namespace vfx::theme
