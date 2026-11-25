#include "ui/theme.h"

#include <QApplication>
#include <QDir>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QStandardPaths>
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
// Style sheets can only use image files for checkbox indicators, so draw them
// for the current palette into the cache dir. Returns the directory.
QString writeIndicators(const QColor& base, const QColor& border, const QColor& accent, const QColor& dim) {
    QString dir = QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).filePath("theme");
    QDir().mkpath(dir);
    auto draw = [&](const QString& name, bool checked, bool disabled) {
        const int s = 36; // drawn at 2x, shown at 18 px
        QPixmap pm(s, s);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        QRectF r(2, 2, s - 4, s - 4);
        QColor fill = checked ? (disabled ? dim : accent) : base;
        p.setPen(QPen(checked ? fill : (disabled ? dim : border), 2.5));
        p.setBrush(fill);
        p.drawRoundedRect(r, 7, 7);
        if (checked) {
            QPainterPath tick;
            tick.moveTo(10, 18.5);
            tick.lineTo(15.5, 24);
            tick.lineTo(26, 12);
            p.setPen(QPen(Qt::white, 3.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(Qt::NoBrush);
            p.drawPath(tick);
        }
        p.end();
        pm.save(QDir(dir).filePath(name));
    };
    draw("cb-off.png", false, false);
    draw("cb-on.png", true, false);
    draw("cb-off-dis.png", false, true);
    draw("cb-on-dis.png", true, true);
    return dir;
}

// Rounded, flatter controls on top of Fusion, coloured from the active palette.
QString styleSheetFor(const QPalette& p) {
    const bool dark = p.color(QPalette::Window).lightness() < 128;
    auto c = [](const QColor& x) { return x.name(QColor::HexRgb); };
    const QColor win = p.color(QPalette::Window), base = p.color(QPalette::Base);
    const QColor btn = p.color(QPalette::Button), text = p.color(QPalette::Text);
    const QColor accent = p.color(QPalette::Highlight);
    const QColor border = dark ? win.lighter(165) : win.darker(118);
    const QColor panel = dark ? win.lighter(112) : win.lighter(103);
    const QColor hover = dark ? btn.lighter(118) : btn.darker(104);
    const QColor dim = p.color(QPalette::PlaceholderText);
    const QColor rec = dark ? QColor(0xe5, 0x48, 0x4d) : QColor(0xd0, 0x3b, 0x3f);
    QString s = R"(
QPushButton, QToolButton#barButton {
  background: %BTN%; color: %TEXT%; border: 1px solid %BORDER%; border-radius: 7px; padding: 5px 12px;
}
QPushButton:hover, QToolButton#barButton:hover { background: %HOVER%; }
QPushButton:pressed, QToolButton#barButton:pressed { background: %BORDER%; }
QPushButton:checked, QToolButton#barButton:checked { background: %ACCENT%; border-color: %ACCENT%; color: white; }
QPushButton:disabled { color: %DIM%; background: %PANEL%; }
QPushButton::menu-indicator { image: none; width: 0; }
QCheckBox::indicator, QGroupBox::indicator { width: 18px; height: 18px; }
QCheckBox::indicator:unchecked, QGroupBox::indicator:unchecked { image: url(%IND%/cb-off.png); }
QCheckBox::indicator:checked, QGroupBox::indicator:checked { image: url(%IND%/cb-on.png); }
QCheckBox::indicator:unchecked:disabled, QGroupBox::indicator:unchecked:disabled { image: url(%IND%/cb-off-dis.png); }
QCheckBox::indicator:checked:disabled, QGroupBox::indicator:checked:disabled { image: url(%IND%/cb-on-dis.png); }
QCheckBox { spacing: 7px; }
QPushButton:default, QPushButton#accentButton { background: %ACCENT%; border-color: %ACCENT%; color: white; }
QPushButton:default:disabled, QPushButton#accentButton:disabled { background: %PANEL%; border-color: %BORDER%; color: %DIM%; }
QPushButton#recordButton { background: %REC%; border-color: %REC%; color: white; font-weight: 600; border-radius: 22px; padding: 6px 18px; }
QPushButton#recordButton:hover { background: %RECH%; }
QPushButton#recordButton[recording="true"] { background: %BTN%; color: %REC%; border: 2px solid %REC%; }
QToolButton#segment { background: %BTN%; border: 1px solid %BORDER%; padding: 4px 8px; border-radius: 0; }
QToolButton#segment:checked { background: %ACCENT%; color: white; border-color: %ACCENT%; }
QToolButton#segment[first="true"] { border-top-left-radius: 7px; border-bottom-left-radius: 7px; }
QToolButton#segment[last="true"] { border-top-right-radius: 7px; border-bottom-right-radius: 7px; }
QLineEdit, QComboBox, QSpinBox {
  background: %BASE%; color: %TEXT%; border: 1px solid %BORDER%; border-radius: 7px; padding: 4px 8px;
  selection-background-color: %ACCENT%;
}
QLineEdit:focus, QComboBox:focus { border-color: %ACCENT%; }
QComboBox::drop-down { border: none; width: 22px; }
QComboBox QAbstractItemView { background: %BASE%; border: 1px solid %BORDER%; selection-background-color: %ACCENT%; }
QGroupBox {
  background: %PANEL%; border: 1px solid %BORDER%; border-radius: 10px;
  margin-top: 20px; padding: 10px 8px 6px 8px; font-weight: 600;
}
QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; left: 6px; top: 2px; padding: 0 4px; }
QGroupBox QLabel, QGroupBox QCheckBox, QGroupBox QPushButton, QGroupBox QComboBox, QGroupBox QLineEdit { font-weight: normal; }
QSlider::groove:horizontal { height: 4px; background: %BORDER%; border-radius: 2px; }
QSlider::sub-page:horizontal { background: %ACCENT%; border-radius: 2px; }
QSlider::handle:horizontal { width: 14px; height: 14px; margin: -6px 0; border-radius: 7px; background: %HANDLE%; border: 1px solid %HBORDER%; }
QSlider::handle:horizontal:hover { background: %ACCENT%; border-color: %ACCENT%; }
QSlider::handle:horizontal:disabled { background: %DIM%; }
QSlider::sub-page:horizontal:disabled { background: %DIM%; }
QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; border: none; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar::handle:vertical { background: %BORDER%; border-radius: 4px; min-height: 30px; }
QScrollBar::handle:vertical:hover { background: %DIM%; }
QScrollBar::add-line, QScrollBar::sub-line, QScrollBar::add-page, QScrollBar::sub-page { height: 0; background: none; }
QSplitter::handle { background: transparent; width: 6px; }
QTableWidget { background: %BASE%; border: 1px solid %BORDER%; border-radius: 8px; gridline-color: %BORDER%; }
QHeaderView::section { background: %PANEL%; color: %TEXT%; border: none; padding: 4px 6px; }
QToolTip { background: %PANEL%; color: %TEXT%; border: 1px solid %BORDER%; padding: 4px; }
QStatusBar { color: %DIM%; }
QLabel#dimText { color: %DIM%; }
QLabel#thumb { background: %BASE%; border: 1px solid %BORDER%; border-radius: 8px; color: %DIM%; }
QLabel#bigTime { font-size: 18px; font-weight: 600; }
QWidget#topBar { background: %PANEL%; border: 1px solid %BORDER%; border-radius: 10px; }
QMenu { background: %PANEL%; border: 1px solid %BORDER%; padding: 4px; }
QMenu::item { padding: 5px 18px; border-radius: 5px; }
QMenu::item:selected { background: %ACCENT%; color: white; }
)";
    s.replace("%BTN%", c(btn)).replace("%TEXT%", c(text)).replace("%BORDER%", c(border))
     .replace("%HOVER%", c(hover)).replace("%ACCENT%", c(accent)).replace("%DIM%", c(dim))
     .replace("%PANEL%", c(panel)).replace("%BASE%", c(base)).replace("%REC%", c(rec))
     .replace("%RECH%", c(rec.lighter(115)))
     .replace("%HANDLE%", dark ? c(text) : "#ffffff").replace("%HBORDER%", dark ? c(text) : c(border.darker(115)))
     .replace("%IND%", writeIndicators(base, border.lighter(dark ? 130 : 100), accent, dim));
    return s;
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
        qApp->setStyleSheet(QString());
        g_current = "system";
        return;
    }
    QApplication::setStyle(QStyleFactory::create("Fusion"));
    if (name == "light") {
        QPalette p = QApplication::style()->standardPalette();
        p.setColor(QPalette::Highlight, QColor(0x2f, 0x6f, 0xeb));
        QApplication::setPalette(p);
        g_current = "light";
    } else {
        QApplication::setPalette(darkPalette());
        g_current = "dark";
    }
    qApp->setStyleSheet(styleSheetFor(QApplication::palette()));
}

QString current() { return g_current; }

bool isDark() { return QApplication::palette().color(QPalette::Window).lightness() < 128; }

QColor ok() { return isDark() ? QColor(0x4c, 0xc2, 0x7a) : QColor(0x2e, 0x8b, 0x50); }
QColor warn() { return isDark() ? QColor(0xf0, 0xa5, 0x4a) : QColor(0xb8, 0x6e, 0x10); }
QColor error() { return isDark() ? QColor(0xff, 0x6b, 0x6b) : QColor(0xc0, 0x39, 0x2b); }
QString css(const QColor& c) { return c.name(QColor::HexRgb); }

} // namespace vfx::theme
