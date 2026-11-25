#pragma once
// Soundboard pads: custom-painted cards in a grid that reflows with the window.
// Two layouts: image on top (tiles) or a thumbnail beside the title (list);
// "Auto" picks tiles when pads are wide enough.
#include <QAbstractButton>
#include <QPixmap>
#include <QWidget>

class QVariantAnimation;

namespace vfx {

struct PadInfo {
    QString id;
    QString title;
    QString keys;      // binding shown on the pad, may be empty
    QString mode;      // "", "hold", "loop", "layer"
    QString imagePath; // may be empty
    QString problem;   // load error / "loading…", empty when fine
    QString tooltip;
};

enum class PadLayout { Auto, Tiles, List };

class PadWidget : public QAbstractButton {
    Q_OBJECT
public:
    PadWidget(const PadInfo& info, QWidget* parent = nullptr);
    const PadInfo& info() const { return info_; }
    void setTiles(bool tiles) { tiles_ = tiles; update(); }
    void setProgress(float p); // -1 = not playing
    void flash();              // brief highlight, e.g. when triggered by a key

signals:
    void contextMenu(const QString& id, const QPoint& globalPos);
    void imageDropped(const QString& id, const QString& path);

protected:
    void paintEvent(QPaintEvent*) override;
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;
    void contextMenuEvent(QContextMenuEvent*) override;
    void dragEnterEvent(QDragEnterEvent*) override;
    void dragLeaveEvent(QDragLeaveEvent*) override;
    void dropEvent(QDropEvent*) override;

private:
    void paintArt(QPainter& p, const QRect& r, int radius);
    PadInfo info_;
    QPixmap image_;
    bool tiles_ = true;
    bool hover_ = false;
    bool dropTarget_ = false;
    float progress_ = -1.f;
    float pressAmt_ = 0.f; // 0..1 animated
    QVariantAnimation* pressAnim_;
};

class PadGrid : public QWidget {
    Q_OBJECT
public:
    explicit PadGrid(QWidget* parent = nullptr);
    void setPads(const QList<PadInfo>& pads);
    void setLayoutMode(PadLayout m);
    void setPadSize(int px); // target pad width
    void setEmptyText(const QString& t);
    // Updates playing state; returns true if any pad is playing.
    bool updateProgress(const std::function<float(const QString&)>& progressOf);
    QSize sizeHint() const override { return {600, 400}; }

signals:
    void padPressed(const QString& id);
    void padReleased(const QString& id);
    void padContextMenu(const QString& id, const QPoint& globalPos);
    void imageDropped(const QString& id, const QString& path);

protected:
    void resizeEvent(QResizeEvent*) override;
    void paintEvent(QPaintEvent*) override;

private:
    void relayout();
    QList<PadWidget*> pads_;
    PadLayout mode_ = PadLayout::Auto;
    int padSize_ = 170;
    QString empty_;
};

} // namespace vfx
