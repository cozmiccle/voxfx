#include "ui/meter_widget.h"

#include <QPainter>
#include <cmath>

namespace vfx {

namespace {
constexpr float kFloorDb = -60.f;
float toDb(float x) { return x > 1e-6f ? 20.f * std::log10(x) : -120.f; }
} // namespace

MeterWidget::MeterWidget(QStringList labels, QWidget* parent)
    : QWidget(parent), labels_(std::move(labels)) {
    int n = int(labels_.size());
    levelDb_.fill(-120.f, n);
    holdDb_.fill(-120.f, n);
    holdTicks_.fill(0, n);
    indicators_.fill(false, n);
    setMinimumHeight(120);
}

void MeterWidget::setPeaks(const QVector<float>& lin) {
    for (int i = 0; i < levelDb_.size() && i < lin.size(); ++i) {
        float db = toDb(lin[i]);
        levelDb_[i] = std::max(db, levelDb_[i] - 1.5f); // ~45 dB/s fall at 30 fps
        if (db >= holdDb_[i]) { holdDb_[i] = db; holdTicks_[i] = 30; }
        else if (--holdTicks_[i] <= 0) holdDb_[i] = std::max(holdDb_[i] - 1.f, levelDb_[i]);
    }
    update();
}

void MeterWidget::setIndicator(int i, bool on) {
    if (i >= 0 && i < indicators_.size() && indicators_[i] != on) { indicators_[i] = on; update(); }
}

void MeterWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const int n = int(levelDb_.size());
    const int labelH = fontMetrics().height() + 4;
    const int w = width() / std::max(n, 1);
    const int barW = std::min(26, w - 14);
    const int top = 14, bottom = height() - labelH;
    const QPalette& pal = palette();

    for (int i = 0; i < n; ++i) {
        int x = i * w + (w - barW) / 2;
        QRect track(x, top, barW, bottom - top);
        p.setPen(Qt::NoPen);
        p.setBrush(pal.color(QPalette::Base).darker(115));
        p.drawRoundedRect(track, 3, 3);

        auto yFor = [&](float db) {
            float t = std::clamp((db - kFloorDb) / -kFloorDb, 0.f, 1.f);
            return bottom - int(t * (bottom - top));
        };
        int y = yFor(levelDb_[i]);
        if (y < bottom) {
            QLinearGradient g(0, bottom, 0, top);
            g.setColorAt(0.0, QColor(46, 160, 90));
            g.setColorAt(0.75, QColor(46, 160, 90));
            g.setColorAt(0.86, QColor(220, 180, 40));
            g.setColorAt(1.0, QColor(210, 60, 50));
            p.setBrush(g);
            p.drawRoundedRect(QRect(x, y, barW, bottom - y), 3, 3);
        }
        if (holdDb_[i] > kFloorDb) {
            p.setBrush(pal.color(QPalette::Text));
            p.drawRect(x, yFor(holdDb_[i]), barW, 2);
        }
        if (indicators_[i]) {
            p.setBrush(QColor(46, 180, 90));
            p.drawEllipse(QPoint(x + barW / 2, 6), 4, 4);
        }
        p.setPen(pal.color(QPalette::Text));
        p.drawText(QRect(i * w, bottom + 2, w, labelH), Qt::AlignCenter, labels_[i]);
    }
}

} // namespace vfx
