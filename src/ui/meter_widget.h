#pragma once
#include <QWidget>

namespace vfx {

// Vertical peak meters with decay and a peak-hold tick, one per channel.
class MeterWidget : public QWidget {
    Q_OBJECT
public:
    explicit MeterWidget(QStringList labels, QWidget* parent = nullptr);
    void setPeaks(const QVector<float>& linear); // call ~30x per second
    void setIndicator(int index, bool on);       // small "gate open" style light
    QSize sizeHint() const override { return {220, 150}; }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QStringList labels_;
    QVector<float> levelDb_, holdDb_;
    QVector<int> holdTicks_;
    QVector<bool> indicators_;
};

} // namespace vfx
