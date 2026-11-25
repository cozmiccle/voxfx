#include "ui/sound_dialogs.h"

#include "app.h"
#include "ui/theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QPainter>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <cmath>

namespace vfx {

namespace {
const char* kImageFilter = "Images (*.png *.jpg *.jpeg *.webp *.gif *.bmp)";

QString fmtTime(double s) {
    int m = int(s) / 60;
    return QString("%1:%2").arg(m).arg(s - m * 60, 4, 'f', 1, QChar('0'));
}
} // namespace

// ============================================================================ ImagePicker

ImagePicker::ImagePicker(const QString& current, QWidget* parent) : QWidget(parent) {
    setAcceptDrops(true);
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    thumb_ = new QLabel;
    thumb_->setFixedSize(72, 72);
    thumb_->setAlignment(Qt::AlignCenter);
    thumb_->setObjectName("thumb");
    auto* choose = new QPushButton("Choose…");
    remove_ = new QPushButton("Remove");
    auto* col = new QVBoxLayout;
    col->addWidget(choose);
    col->addWidget(remove_);
    col->addStretch();
    lay->addWidget(thumb_);
    lay->addLayout(col);
    lay->addStretch();
    connect(choose, &QPushButton::clicked, this, [this] {
        QString f = QFileDialog::getOpenFileName(this, "Choose an image", QString(), kImageFilter);
        if (!f.isEmpty()) { set(f); changed_ = true; }
    });
    connect(remove_, &QPushButton::clicked, this, [this] { set(QString()); changed_ = true; });
    set(current);
    setToolTip("Optional picture for the pad. You can also drop an image here.");
}

void ImagePicker::set(const QString& p) {
    path_ = p;
    remove_->setEnabled(!p.isEmpty());
    if (p.isEmpty()) {
        thumb_->setPixmap({});
        thumb_->setText("No image");
        return;
    }
    QImageReader r(p);
    r.setAutoTransform(true);
    QSize s = r.size();
    if (s.isValid()) r.setScaledSize(s.scaled(144, 144, Qt::KeepAspectRatioByExpanding));
    QImage img = r.read();
    if (img.isNull()) { thumb_->setText("Can't read"); return; }
    int side = std::min(img.width(), img.height());
    img = img.copy((img.width() - side) / 2, (img.height() - side) / 2, side, side);
    QPixmap pm = QPixmap::fromImage(img.scaled(72, 72, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
    thumb_->setPixmap(pm);
}

void ImagePicker::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasUrls() && e->mimeData()->urls().first().isLocalFile()) e->acceptProposedAction();
}

void ImagePicker::dropEvent(QDropEvent* e) {
    QString f = e->mimeData()->urls().first().toLocalFile();
    if (!QImageReader(f).canRead()) return;
    set(f);
    changed_ = true;
}

// ============================================================================ LevelBar

void LevelBar::setLevel(float lin) {
    float db = lin > 1e-6f ? 20.f * std::log10(lin) : -90.f;
    db_ = std::max(db, db_ - 2.f);
    update();
}

void LevelBar::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(Qt::NoPen);
    p.setBrush(palette().color(QPalette::Base));
    p.drawRoundedRect(r, 4, 4);
    float t = std::clamp((db_ + 60.f) / 60.f, 0.f, 1.f);
    if (t <= 0) return;
    QColor c = db_ > -3.f ? theme::error() : db_ > -12.f ? theme::warn() : theme::ok();
    p.setBrush(c);
    p.drawRoundedRect(QRectF(r.left(), r.top(), r.width() * t, r.height()), 4, 4);
}

// ============================================================================ RecordDialog

RecordDialog::RecordDialog(App& app, QWidget* parent) : QDialog(parent), app_(app) {
    setWindowTitle("Record a sound");
    auto* lay = new QVBoxLayout(this);
    lay->setSpacing(10);

    Params& p = app.engine().params();
    QString effect = p.fxOn ? presetName(VoicePreset(p.preset.load())) : "None";
    auto* note = new QLabel(QString("Records your voice the way others hear it: cleanup %1, voice effect "
                                    "<b>%2</b>. Soundboard sounds aren't included. Up to %3 seconds.")
                                .arg(p.cleanupOn ? "on" : "off", effect)
                                .arg(int(Recorder::kMaxSeconds)));
    note->setWordWrap(true);
    lay->addWidget(note);

    // Record button + time + level.
    auto* recRow = new QHBoxLayout;
    rec_ = new QPushButton("●  Record");
    rec_->setObjectName("recordButton");
    rec_->setMinimumSize(130, 44);
    rec_->setDefault(true);
    time_ = new QLabel(fmtTime(0) + " / " + fmtTime(Recorder::kMaxSeconds));
    time_->setObjectName("bigTime");
    level_ = new LevelBar;
    level_->setMinimumHeight(10);
    auto* tl = new QVBoxLayout;
    tl->addWidget(time_);
    tl->addWidget(level_);
    recRow->addWidget(rec_);
    recRow->addSpacing(8);
    recRow->addLayout(tl, 1);
    lay->addLayout(recRow);

    state_ = new QLabel;
    state_->setWordWrap(true);
    lay->addWidget(state_);

    auto* form = new QFormLayout;
    name_ = new QLineEdit(QString("Recording %1").arg(app.library().sounds().size() + 1));
    name_->selectAll();
    form->addRow("Name", name_);
    image_ = new ImagePicker(QString());
    form->addRow("Image", image_);
    trim_ = new QCheckBox("Trim silence at the start and end");
    trim_->setChecked(true);
    form->addRow("", trim_);
    lay->addLayout(form);

    auto* buttons = new QDialogButtonBox;
    preview_ = buttons->addButton("▶  Preview", QDialogButtonBox::ActionRole);
    preview_->setToolTip("Plays the take in your headphones only.");
    save_ = buttons->addButton("Save sound", QDialogButtonBox::AcceptRole);
    save_->setObjectName("accentButton");
    buttons->addButton(QDialogButtonBox::Cancel);
    preview_->setEnabled(false);
    save_->setEnabled(false);
    lay->addWidget(buttons);

    connect(rec_, &QPushButton::clicked, this, &RecordDialog::toggle);
    connect(preview_, &QPushButton::clicked, this, [this] { if (!take_.isEmpty()) app_.previewFile(take_); });
    connect(buttons, &QDialogButtonBox::accepted, this, &RecordDialog::save);
    connect(buttons, &QDialogButtonBox::rejected, this, &RecordDialog::reject);

    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, &RecordDialog::tick);
    timer_->start(33);

    if (app.engine().status().micName.empty())
        setState("No microphone is connected, so there's nothing to record yet.", true);
    else
        setState("Press Record (or Space) and talk. Press again to stop.");
    rec_->setFocus();
    resize(460, sizeHint().height());
}

RecordDialog::~RecordDialog() {
    if (app_.isRecording()) {
        QString err;
        QString f = app_.stopRecording(&err, false);
        if (!f.isEmpty()) app_.discardRecording(f);
    }
}

void RecordDialog::setState(const QString& text, bool error) {
    state_->setText(error ? QString("<span style='color:%1'>%2</span>").arg(theme::css(theme::error()), text.toHtmlEscaped())
                          : text.toHtmlEscaped());
}

void RecordDialog::toggle() {
    if (app_.isRecording()) stopTake();
    else startTake();
}

void RecordDialog::startTake() {
    if (!take_.isEmpty()) { app_.discardRecording(take_); take_.clear(); }
    QString err;
    if (!app_.startRecording(&err)) { setState("Couldn't start recording: " + err, true); return; }
    rec_->setText("■  Stop");
    rec_->setProperty("recording", true);
    rec_->style()->unpolish(rec_);
    rec_->style()->polish(rec_);
    preview_->setEnabled(false);
    save_->setEnabled(false);
    setState("Recording…");
}

void RecordDialog::stopTake() {
    QString err;
    take_ = app_.stopRecording(&err, trim_->isChecked());
    rec_->setText("●  Record again");
    rec_->setProperty("recording", false);
    rec_->style()->unpolish(rec_);
    rec_->style()->polish(rec_);
    if (take_.isEmpty()) { setState(err, true); return; }
    preview_->setEnabled(true);
    save_->setEnabled(true);
    save_->setDefault(true);
    setState("Got it. Preview it, give it a name, then save.");
    name_->setFocus();
    name_->selectAll();
}

void RecordDialog::tick() {
    level_->setLevel(app_.engine().meters().micOut.load());
    if (app_.isRecording()) {
        time_->setText(fmtTime(app_.recordingSeconds()) + " / " + fmtTime(Recorder::kMaxSeconds));
        if (app_.recordingHitLimit()) stopTake();
    }
}

void RecordDialog::save() {
    if (take_.isEmpty()) return;
    app_.addRecording(take_, name_->text(), image_->path());
    take_.clear();
    QDialog::accept();
}

void RecordDialog::reject() {
    if (!take_.isEmpty()) { app_.discardRecording(take_); take_.clear(); }
    QDialog::reject();
}

// ============================================================================ SoundDialog

SoundDialog::SoundDialog(App& app, const QString& id, QWidget* parent) : QDialog(parent), app_(app), id_(id) {
    const SoundEntry* e = app.library().find(id);
    setWindowTitle("Edit sound");
    auto* lay = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    name_ = new QLineEdit(e ? e->title : QString());
    form->addRow("Name", name_);
    image_ = new ImagePicker(e ? e->image : QString());
    form->addRow("Image", image_);

    auto* trimRow = new QHBoxLayout;
    trim_ = new QSlider(Qt::Horizontal);
    trim_->setRange(-24, 12);
    trim_->setValue(e ? int(e->trimDb) : 0);
    auto* trimVal = new QLabel(QString("%1 dB").arg(trim_->value()));
    trimVal->setMinimumWidth(48);
    connect(trim_, &QSlider::valueChanged, trimVal, [trimVal](int v) { trimVal->setText(QString("%1 dB").arg(v)); });
    trimRow->addWidget(trim_, 1);
    trimRow->addWidget(trimVal);
    form->addRow("Volume", trimRow);

    mode_ = new QComboBox;
    mode_->addItem("Play once (press again restarts)", int(PlayMode::Restart));
    mode_->addItem("Play once (presses layer)", int(PlayMode::Overlap));
    mode_->addItem("Play while held", int(PlayMode::Hold));
    mode_->addItem("Loop until pressed again", int(PlayMode::ToggleLoop));
    mode_->setCurrentIndex(std::max(0, mode_->findData(int(e ? e->mode : PlayMode::Restart))));
    form->addRow("Play mode", mode_);

    QString info = app.clipInfo(id);
    if (e && !e->sourceUrl.isEmpty())
        info += QString("<br><a href='%1'>Source page</a>").arg(e->sourceUrl.toHtmlEscaped());
    auto* infoL = new QLabel(info);
    infoL->setOpenExternalLinks(true);
    infoL->setObjectName("dimText");
    form->addRow("", infoL);
    lay->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    auto* preview = buttons->addButton("▶  Preview", QDialogButtonBox::ActionRole);
    preview->setToolTip("Plays in your headphones only (applies the volume after saving).");
    connect(preview, &QPushButton::clicked, this, [this] { app_.previewSound(id_); });
    connect(buttons, &QDialogButtonBox::accepted, this, &SoundDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(buttons);
    resize(440, sizeHint().height());
}

void SoundDialog::accept() {
    const SoundEntry* e = app_.library().find(id_);
    if (!e) { QDialog::accept(); return; }
    if (name_->text().trimmed() != e->title) app_.rename(id_, name_->text());
    if (float(trim_->value()) != e->trimDb) app_.setTrim(id_, float(trim_->value()));
    PlayMode m = PlayMode(mode_->currentData().toInt());
    if (m != e->mode) app_.setMode(id_, m);
    if (image_->changed()) app_.setImage(id_, image_->path());
    QDialog::accept();
}

} // namespace vfx
