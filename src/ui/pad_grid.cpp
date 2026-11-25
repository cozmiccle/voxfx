#include "ui/pad_grid.h"

#include "ui/theme.h"

#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QFileInfo>
#include <QHash>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QVariantAnimation>

namespace vfx {

namespace {

constexpr int kGap = 10, kMargin = 6, kListH = 66, kTextH = 50;

QPixmap cachedImage(const QString& path) {
    static QHash<QString, QPixmap> cache;
    if (path.isEmpty()) return {};
    auto it = cache.find(path);
    if (it != cache.end()) return *it;
    QPixmap pm(path);
    if (cache.size() > 400) cache.clear();
    cache.insert(path, pm);
    return pm;
}

bool isImageFile(const QString& path) {
    static const QStringList ext = {"png", "jpg", "jpeg", "webp", "gif", "bmp"};
    return ext.contains(QFileInfo(path).suffix().toLower());
}

QString droppedImage(const QMimeData* m) {
    if (!m->hasUrls()) return {};
    for (const QUrl& u : m->urls())
        if (u.isLocalFile() && isImageFile(u.toLocalFile())) return u.toLocalFile();
    return {};
}

QColor blend(const QColor& a, const QColor& b, float t) {
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t, a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t);
}

} // namespace

// ============================================================================ PadWidget

PadWidget::PadWidget(const PadInfo& info, QWidget* parent)
    : QAbstractButton(parent), info_(info), image_(cachedImage(info.imagePath)) {
    setToolTip(info.tooltip);
    setCursor(Qt::PointingHandCursor);
    setAcceptDrops(true);
    setAttribute(Qt::WA_Hover);
    pressAnim_ = new QVariantAnimation(this);
    connect(pressAnim_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        pressAmt_ = v.toFloat();
        update();
    });
    connect(this, &QAbstractButton::pressed, this, [this] {
        pressAnim_->stop();
        pressAnim_->setDuration(60);
        pressAnim_->setStartValue(pressAmt_);
        pressAnim_->setEndValue(1.f);
        pressAnim_->start();
    });
    connect(this, &QAbstractButton::released, this, [this] {
        pressAnim_->stop();
        pressAnim_->setDuration(220);
        pressAnim_->setStartValue(pressAmt_);
        pressAnim_->setEndValue(0.f);
        pressAnim_->start();
    });
}

void PadWidget::setProgress(float p) {
    if (std::abs(p - progress_) < 0.002f) return;
    progress_ = p;
    update();
}

void PadWidget::flash() {
    pressAnim_->stop();
    pressAnim_->setDuration(350);
    pressAnim_->setStartValue(0.8f);
    pressAnim_->setEndValue(0.f);
    pressAnim_->start();
}

void PadWidget::enterEvent(QEnterEvent*) { hover_ = true; update(); }
void PadWidget::leaveEvent(QEvent*) { hover_ = false; update(); }

void PadWidget::contextMenuEvent(QContextMenuEvent* e) { emit contextMenu(info_.id, e->globalPos()); }

void PadWidget::dragEnterEvent(QDragEnterEvent* e) {
    if (droppedImage(e->mimeData()).isEmpty()) { e->ignore(); return; } // audio: let the window take it
    dropTarget_ = true;
    update();
    e->acceptProposedAction();
}
void PadWidget::dragLeaveEvent(QDragLeaveEvent*) { dropTarget_ = false; update(); }
void PadWidget::dropEvent(QDropEvent* e) {
    dropTarget_ = false;
    update();
    QString img = droppedImage(e->mimeData());
    if (img.isEmpty()) { e->ignore(); return; }
    e->acceptProposedAction();
    emit imageDropped(info_.id, img);
}

void PadWidget::paintArt(QPainter& p, const QRect& r, int radius) {
    QPainterPath clip;
    clip.addRoundedRect(QRectF(r), radius, radius);
    p.save();
    p.setClipPath(clip);
    if (!image_.isNull()) {
        // Cover: scale to fill, crop the overflow.
        QSize s = image_.size().scaled(r.size(), Qt::KeepAspectRatioByExpanding);
        QRect src(0, 0, image_.width(), image_.height());
        double fx = double(image_.width()) / s.width(), fy = double(image_.height()) / s.height();
        int cropW = int(r.width() * fx), cropH = int(r.height() * fy);
        src = QRect((image_.width() - cropW) / 2, (image_.height() - cropH) / 2, cropW, cropH);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawPixmap(r, image_, src);
    } else {
        // Placeholder: a colour derived from the title, with its first letter.
        const int hue = int(qHash(info_.title) % 360);
        const bool dark = theme::isDark();
        QLinearGradient g(r.topLeft(), r.bottomRight());
        g.setColorAt(0, QColor::fromHsl(hue, 120, dark ? 82 : 175));
        g.setColorAt(1, QColor::fromHsl((hue + 40) % 360, 120, dark ? 52 : 140));
        p.fillRect(r, g);
        QFont f = font();
        f.setBold(true);
        f.setPixelSize(std::max(14, int(std::min(r.width(), r.height()) * 0.42)));
        p.setFont(f);
        p.setPen(QColor(255, 255, 255, dark ? 200 : 235));
        QString letter = info_.title.trimmed().left(1).toUpper();
        p.drawText(r, Qt::AlignCenter, letter.isEmpty() ? "♪" : letter);
    }
    p.restore();
}

void PadWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPalette& pal = palette();
    const QColor accent = pal.color(QPalette::Highlight);
    const bool playing = progress_ >= 0.f;
    const int radius = 10;

    // Press: shrink slightly and darken.
    const float shrink = 3.f * pressAmt_;
    QRectF card = QRectF(rect()).adjusted(2 + shrink, 2 + shrink, -2 - shrink, -2 - shrink);

    QColor bg = pal.color(QPalette::Button);
    if (hover_) bg = bg.lighter(theme::isDark() ? 118 : 104);
    bg = blend(bg, accent, playing ? 0.18f : 0.f);
    bg = bg.darker(100 + int(25 * pressAmt_));

    if (playing) { // soft glow
        QColor glow = accent;
        for (int i = 3; i >= 1; --i) {
            glow.setAlpha(28 * (4 - i));
            p.setPen(Qt::NoPen);
            p.setBrush(glow);
            p.drawRoundedRect(card.adjusted(-i, -i, i, i), radius + i, radius + i);
        }
    }
    p.setPen(QPen(dropTarget_ ? accent : playing ? accent : pal.color(QPalette::Mid),
                  dropTarget_ || playing ? 2.0 : 1.0, dropTarget_ ? Qt::DashLine : Qt::SolidLine));
    p.setBrush(bg);
    p.drawRoundedRect(card, radius, radius);

    QRect c = card.toRect();
    QRect art, text;
    if (tiles_) {
        art = QRect(c.left() + 6, c.top() + 6, c.width() - 12, c.height() - kTextH - 6);
        text = QRect(c.left() + 10, art.bottom() + 4, c.width() - 20, kTextH - 10);
    } else {
        int s = c.height() - 14;
        art = QRect(c.left() + 7, c.top() + 7, s, s);
        text = QRect(art.right() + 10, c.top() + 6, c.right() - art.right() - 18, c.height() - 12);
    }
    if (art.height() > 8) paintArt(p, art, tiles_ ? 7 : 6);

    // Title.
    QFont tf = font();
    tf.setBold(true);
    p.setFont(tf);
    p.setPen(pal.color(QPalette::ButtonText));
    QFontMetrics fm(tf);
    int lineH = fm.height();
    QRect titleR(text.left(), text.top() + (text.height() - 2 * lineH - 4) / 2, text.width(), lineH);
    p.drawText(titleR, Qt::AlignLeft | Qt::AlignVCenter, fm.elidedText(info_.title, Qt::ElideRight, text.width()));

    // Second line: problem, or key chip + mode.
    QFont sf = font();
    sf.setPointSizeF(std::max(7.0, sf.pointSizeF() - 1.5));
    p.setFont(sf);
    QFontMetrics sfm(sf);
    QRect sub(text.left(), titleR.bottom() + 4, text.width(), sfm.height() + 4);
    if (!info_.problem.isEmpty()) {
        p.setPen(info_.problem == "loading…" ? pal.color(QPalette::PlaceholderText) : theme::error());
        p.drawText(sub, Qt::AlignLeft | Qt::AlignVCenter, sfm.elidedText(info_.problem, Qt::ElideRight, sub.width()));
    } else {
        int x = sub.left();
        if (!info_.keys.isEmpty()) {
            QString k = sfm.elidedText(info_.keys, Qt::ElideRight, sub.width() - 12);
            int w = sfm.horizontalAdvance(k) + 12;
            QRect chip(x, sub.top(), w, sub.height());
            p.setPen(Qt::NoPen);
            p.setBrush(pal.color(QPalette::Base));
            p.drawRoundedRect(chip, 4, 4);
            p.setPen(pal.color(QPalette::Text));
            p.drawText(chip, Qt::AlignCenter, k);
            x += w + 6;
        }
        if (!info_.mode.isEmpty() && x < sub.right() - 20) {
            p.setPen(pal.color(QPalette::PlaceholderText));
            p.drawText(QRect(x, sub.top(), sub.right() - x, sub.height()), Qt::AlignLeft | Qt::AlignVCenter,
                       info_.mode);
        }
    }

    // Playback progress along the bottom edge.
    if (playing) {
        QRectF bar(card.left() + radius / 2.0, card.bottom() - 5, (card.width() - radius) * progress_, 3);
        p.setPen(Qt::NoPen);
        p.setBrush(accent);
        p.drawRoundedRect(bar, 1.5, 1.5);
    }
    if (hasFocus()) {
        p.setPen(QPen(accent, 1, Qt::DotLine));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(card.adjusted(3, 3, -3, -3), radius - 2, radius - 2);
    }
}

// ============================================================================ PadGrid

PadGrid::PadGrid(QWidget* parent) : QWidget(parent) {}

void PadGrid::setPads(const QList<PadInfo>& pads) {
    qDeleteAll(pads_);
    pads_.clear();
    for (const PadInfo& info : pads) {
        auto* w = new PadWidget(info, this);
        const QString id = info.id;
        connect(w, &QAbstractButton::pressed, this, [this, id] { emit padPressed(id); });
        connect(w, &QAbstractButton::released, this, [this, id] { emit padReleased(id); });
        connect(w, &PadWidget::contextMenu, this, &PadGrid::padContextMenu);
        connect(w, &PadWidget::imageDropped, this, &PadGrid::imageDropped);
        pads_.push_back(w);
        w->show();
    }
    relayout();
    update();
}

void PadGrid::setLayoutMode(PadLayout m) { mode_ = m; relayout(); }
void PadGrid::setPadSize(int px) { padSize_ = px; relayout(); }
void PadGrid::setEmptyText(const QString& t) { empty_ = t; update(); }

bool PadGrid::updateProgress(const std::function<float(const QString&)>& progressOf) {
    bool any = false;
    for (PadWidget* w : pads_) {
        float p = progressOf(w->info().id);
        w->setProgress(p);
        any |= p >= 0.f;
    }
    return any;
}

void PadGrid::resizeEvent(QResizeEvent*) { relayout(); }

void PadGrid::relayout() {
    const int W = width() - 2 * kMargin;
    const bool tiles = mode_ == PadLayout::Tiles || (mode_ == PadLayout::Auto && padSize_ >= 150);
    const int target = tiles ? padSize_ : std::max(200, int(padSize_ * 1.5));
    const int cols = std::max(1, (W + kGap) / (target + kGap));
    const int w = std::max(80, (W - kGap * (cols - 1)) / cols);
    const int h = tiles ? std::max(110, int(w * 0.62) + kTextH) : kListH;
    int i = 0;
    for (PadWidget* p : pads_) {
        p->setTiles(tiles);
        p->setGeometry(kMargin + (i % cols) * (w + kGap), kMargin + (i / cols) * (h + kGap), w, h);
        ++i;
    }
    const int rows = (int(pads_.size()) + cols - 1) / cols;
    setMinimumHeight(rows ? 2 * kMargin + rows * h + (rows - 1) * kGap : 0);
}

void PadGrid::paintEvent(QPaintEvent*) {
    if (!pads_.isEmpty() || empty_.isEmpty()) return;
    QPainter p(this);
    p.setPen(palette().color(QPalette::PlaceholderText));
    p.drawText(rect().adjusted(20, 20, -20, -20), Qt::AlignCenter | Qt::TextWordWrap, empty_);
}

} // namespace vfx
