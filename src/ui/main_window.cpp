#include "ui/main_window.h"

#include "app.h"
#include "ui/bind_dialog.h"
#include "ui/devices_dialog.h"
#include "ui/meter_widget.h"
#include "ui/theme.h"
#include "dsp/rnnoise_wrap.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace vfx {

namespace {
constexpr int kPadCols = 4;
QString dbText(float db) { return QString("%1 dB").arg(double(db), 0, 'f', 0); }
} // namespace

MainWindow::MainWindow(App& app, QWidget* parent) : QMainWindow(parent), app_(app) {
    setWindowTitle("voxfx");
    setAcceptDrops(true);

    auto* split = new QSplitter;
    split->addWidget(buildControls());
    split->addWidget(buildBoard());
    split->setStretchFactor(1, 1);
    split->setCollapsible(0, false);
    setCentralWidget(split);
    resize(1060, 700);

    buildTray();
    refreshMics();
    syncFromParams();
    rebuildPads();

    connect(&app_, &App::soundsChanged, this, &MainWindow::rebuildPads);
    connect(&app_, &App::bindingsChanged, this, &MainWindow::rebuildPads);
    connect(&app_, &App::paramsChanged, this, &MainWindow::syncFromParams);
    connect(&app_, &App::bankChanged, this, [this](int b) {
        bankLabel_->setText(QString("Bank %1").arg(b + 1));
        rebuildPads();
    });
    connect(&app_, &App::message, this, [this](const QString& m) { statusBar()->showMessage(m, 6000); });
    connect(&app_, &App::showWindowRequested, this, [this] { show(); raise(); activateWindow(); });

    // PipeWire callbacks arrive on its thread; bounce to the GUI thread.
    app_.engine().setChangedCallback([this] {
        QMetaObject::invokeMethod(this, [this] { refreshMics(); refreshStatus(); }, Qt::QueuedConnection);
    });

    auto* meterTimer = new QTimer(this);
    connect(meterTimer, &QTimer::timeout, this, &MainWindow::tickMeters);
    meterTimer->start(33);
    auto* statusTimer = new QTimer(this);
    connect(statusTimer, &QTimer::timeout, this, &MainWindow::refreshStatus);
    statusTimer->start(1000);
    refreshStatus();
}

// ============================================================================ controls

QSlider* MainWindow::dbSlider(int min, int max, float value, QLabel* readout, std::function<void(float)> set) {
    auto* s = new QSlider(Qt::Horizontal);
    s->setRange(min, max);
    s->setValue(int(std::lround(value)));
    readout->setText(dbText(value));
    readout->setMinimumWidth(48);
    connect(s, &QSlider::valueChanged, this, [readout, set](int v) {
        readout->setText(dbText(float(v)));
        set(float(v));
    });
    return s;
}

QWidget* MainWindow::buildControls() {
    Params& p = app_.engine().params();
    auto* host = new QWidget;
    auto* lay = new QVBoxLayout(host);

    auto row = [](QWidget* a, QWidget* b) {
        auto* w = new QWidget;
        auto* l = new QHBoxLayout(w);
        l->setContentsMargins(0, 0, 0, 0);
        l->addWidget(a, 1);
        l->addWidget(b);
        return w;
    };

    // ---- microphone
    auto* micBox = new QGroupBox("Microphone");
    auto* mf = new QFormLayout(micBox);
    mic_ = new QComboBox;
    connect(mic_, &QComboBox::activated, this, [this](int i) { app_.setMicrophone(mic_->itemData(i).toString()); });
    mf->addRow(mic_);
    micStatus_ = new QLabel;
    micStatus_->setWordWrap(true);
    micStatus_->setTextFormat(Qt::RichText);
    mf->addRow(micStatus_);
    micGainVal_ = new QLabel;
    micGain_ = dbSlider(-20, 20, p.micGainDb, micGainVal_, [&p](float v) { p.micGainDb = v; });
    mf->addRow("Gain", row(micGain_, micGainVal_));
    mute_ = new QPushButton("Mute mic");
    mute_->setCheckable(true);
    connect(mute_, &QPushButton::toggled, this, [this, &p](bool on) {
        p.micMute = on;
        if (trayMute_) { QSignalBlocker b(trayMute_); trayMute_->setChecked(on); }
    });
    ptt_ = new QCheckBox("Push-to-talk");
    ptt_->setToolTip("Mic is silent unless the push-to-talk binding is held.\n"
                     "Add one under Bindings → Add action binding.");
    connect(ptt_, &QCheckBox::toggled, this, [&p](bool on) { p.pttEnabled = on; });
    mf->addRow(row(mute_, ptt_));
    lay->addWidget(micBox);

    meters_ = new MeterWidget({"Mic", "Voice", "Sounds", "Out"});
    meters_->setToolTip("Mic: raw input · Voice: after cleanup/effects · Sounds: soundboard · "
                        "Out: what others hear.\nThe green dot over Voice means the gate is open.");
    lay->addWidget(meters_);

    // ---- cleanup
    cleanup_ = new QGroupBox("Cleanup (filter, noise, gate)");
    cleanup_->setCheckable(true);
    connect(cleanup_, &QGroupBox::toggled, this, [&p](bool on) { p.cleanupOn = on; });
    auto* cf = new QFormLayout(cleanup_);
    nsMix_ = new QSlider(Qt::Horizontal);
    nsMix_->setRange(0, 100);
    connect(nsMix_, &QSlider::valueChanged, this, [&p](int v) { p.nsMix = v / 100.f; });
    if (dsp::Denoiser::available()) {
        cf->addRow("Noise removal", nsMix_);
    } else {
        nsMix_->hide();
        auto* l = new QLabel("<small>Noise removal needs RNNoise (rebuild with librnnoise). "
                             "The gate still cuts background noise between words.</small>");
        l->setWordWrap(true);
        cf->addRow(l);
    }
    gateVal_ = new QLabel;
    gate_ = dbSlider(-80, -20, p.gateThreshDb, gateVal_, [&p](float v) { p.gateThreshDb = v; });
    cf->addRow("Gate", row(gate_, gateVal_));
    calibrate_ = new QPushButton("Calibrate gate (stay quiet 2 s)");
    connect(calibrate_, &QPushButton::clicked, this, &MainWindow::calibrateGate);
    cf->addRow(calibrate_);
    lay->addWidget(cleanup_);

    // ---- voice
    voice_ = new QGroupBox("Voice effect");
    voice_->setCheckable(true);
    connect(voice_, &QGroupBox::toggled, this, [this, &p](bool on) {
        p.fxOn = on;
        if (trayFx_) { QSignalBlocker b(trayFx_); trayFx_->setChecked(on); }
    });
    auto* vf = new QFormLayout(voice_);
    preset_ = new QComboBox;
    for (int i = 0; i < int(VoicePreset::Count); ++i) preset_->addItem(presetName(VoicePreset(i)), i);
    connect(preset_, &QComboBox::currentIndexChanged, this, [&p](int i) { p.preset = i; });
    vf->addRow("Preset", preset_);
    comp_ = new QCheckBox("Compressor (even out loud/quiet)");
    connect(comp_, &QCheckBox::toggled, this, [&p](bool on) { p.compOn = on; });
    limiter_ = new QCheckBox("Limiter (never clip)");
    connect(limiter_, &QCheckBox::toggled, this, [&p](bool on) { p.limiterOn = on; });
    vf->addRow(comp_);
    vf->addRow(limiter_);
    lay->addWidget(voice_);

    // ---- soundboard levels
    auto* sb = new QGroupBox("Soundboard");
    auto* sf = new QFormLayout(sb);
    sfxGainVal_ = new QLabel;
    sfxGain_ = dbSlider(-30, 10, p.sfxGainDb, sfxGainVal_, [&p](float v) { p.sfxGainDb = v; });
    sf->addRow("Volume", row(sfxGain_, sfxGainVal_));
    duckVal_ = new QLabel;
    duck_ = dbSlider(-24, 0, p.duckDb, duckVal_, [&p](float v) { p.duckDb = v; });
    duck_->setToolTip("How much quieter your voice gets while a sound plays (0 = off).");
    sf->addRow("Duck voice", row(duck_, duckVal_));
    lay->addWidget(sb);

    // ---- monitor
    monitor_ = new QGroupBox("Hear it myself");
    monitor_->setCheckable(true);
    connect(monitor_, &QGroupBox::toggled, this, [this](bool on) { app_.engine().setMonitor(on); });
    auto* mof = new QFormLayout(monitor_);
    monGainVal_ = new QLabel;
    monGain_ = dbSlider(-40, 6, p.monitorGainDb, monGainVal_, [&p](float v) { p.monitorGainDb = v; });
    mof->addRow("Volume", row(monGain_, monGainVal_));
    monMic_ = new QCheckBox("Include my voice (use headphones)");
    connect(monMic_, &QCheckBox::toggled, this, [&p](bool on) { p.monitorMic = on; });
    mof->addRow(monMic_);
    lay->addWidget(monitor_);

    // ---- appearance
    auto* look = new QComboBox;
    look->addItem("Dark", "dark");
    look->addItem("Light", "light");
    look->addItem("Follow desktop", "system");
    look->setCurrentIndex(std::max(0, look->findData(app_.settings().theme)));
    connect(look, &QComboBox::activated, this, [this, look](int i) {
        QString name = look->itemData(i).toString();
        theme::apply(name);
        app_.settings().theme = name;
        app_.saveSettings();
        refreshStatus();
        rebuildPads(); // re-colour error labels
    });
    auto* lookRow = new QFormLayout;
    lookRow->addRow("Theme", look);
    lay->addLayout(lookRow);
    lay->addStretch();

    auto* scroll = new QScrollArea;
    scroll->setWidget(host);
    scroll->setWidgetResizable(true);
    scroll->setMinimumWidth(340);
    scroll->setFrameShape(QFrame::NoFrame);
    return scroll;
}

void MainWindow::syncFromParams() {
    Params& p = app_.engine().params();
    QSignalBlocker b1(mute_), b2(ptt_), b3(cleanup_), b4(nsMix_), b5(voice_), b6(preset_),
        b7(comp_), b8(limiter_), b9(monitor_), b10(monMic_), b11(gate_);
    mute_->setChecked(p.micMute);
    ptt_->setChecked(p.pttEnabled);
    cleanup_->setChecked(p.cleanupOn);
    nsMix_->setValue(int(p.nsMix * 100));
    voice_->setChecked(p.fxOn);
    preset_->setCurrentIndex(p.preset);
    comp_->setChecked(p.compOn);
    limiter_->setChecked(p.limiterOn);
    monitor_->setChecked(p.monitorOn);
    monMic_->setChecked(p.monitorMic);
    gate_->setValue(int(std::lround(p.gateThreshDb.load())));
    gateVal_->setText(dbText(p.gateThreshDb));
    if (trayMute_) { QSignalBlocker b(trayMute_); trayMute_->setChecked(p.micMute); }
    if (trayFx_) { QSignalBlocker b(trayFx_); trayFx_->setChecked(p.fxOn); }
}

void MainWindow::refreshMics() {
    auto mics = app_.engine().microphones();
    QString current = app_.settings().micName;
    QSignalBlocker block(mic_);
    mic_->clear();
    mic_->addItem("System default microphone", QString());
    for (auto& m : mics)
        mic_->addItem(QString::fromStdString(m.description), QString::fromStdString(m.name));
    int i = mic_->findData(current);
    if (i < 0 && !current.isEmpty()) {
        mic_->addItem(current + " (not connected)", current);
        i = mic_->count() - 1;
    }
    mic_->setCurrentIndex(std::max(i, 0));
}

void MainWindow::refreshStatus() {
    EngineStatus s = app_.engine().status();
    Meters& m = app_.engine().meters();
    QString txt;
    auto dot = [](const QColor& c, const QString& t) {
        return QString("<span style='color:%1'>● %2</span>").arg(theme::css(c), t);
    };
    if (!s.connected) txt = dot(theme::error(), "Not connected to PipeWire");
    else if (!s.virtualMicReady) txt = dot(theme::warn(), "Creating voxfx Mic…");
    else if (s.micName.empty()) txt = dot(theme::warn(), "No microphone found");
    else txt = dot(theme::ok(), "Ready") + " — choose <b>voxfx Mic</b> as the microphone in Discord/your game.";
    micStatus_->setText(txt);
    statusBar()->showMessage(QString("%1 Hz · %2 sounds playing · %3")
                                 .arg(m.sampleRate.load())
                                 .arg(m.activeVoices.load())
                                 .arg(s.micName.empty() ? QString("no mic") : QString::fromStdString(s.micName)),
                             0);
}

void MainWindow::tickMeters() {
    Meters& m = app_.engine().meters();
    meters_->setPeaks({m.micIn.exchange(0.f), m.micOut.exchange(0.f), m.sfx.exchange(0.f), m.out.exchange(0.f)});
    meters_->setIndicator(1, m.gateOpen.load() && app_.engine().params().cleanupOn.load());
}

void MainWindow::calibrateGate() {
    calibrate_->setEnabled(false);
    calibrate_->setText("Listening… stay quiet");
    auto* samples = new QVector<float>;
    auto* t = new QTimer(this);
    connect(t, &QTimer::timeout, this, [this, t, samples] {
        samples->push_back(app_.engine().meters().noiseFloorDb.load());
        if (samples->size() < 40) return;
        t->stop();
        t->deleteLater();
        std::sort(samples->begin(), samples->end());
        float floorDb = (*samples)[int(samples->size() * 0.9)]; // ignore a stray click
        delete samples;
        float thr = std::clamp(floorDb + 8.f, -80.f, -20.f);
        app_.engine().params().gateThreshDb = thr;
        syncFromParams();
        calibrate_->setEnabled(true);
        calibrate_->setText("Calibrate gate (stay quiet 2 s)");
        statusBar()->showMessage(QString("Noise floor %1 dB → gate set to %2 dB").arg(floorDb, 0, 'f', 0).arg(thr, 0, 'f', 0), 6000);
    });
    t->start(50);
}

// ============================================================================ soundboard

QWidget* MainWindow::buildBoard() {
    auto* host = new QWidget;
    auto* lay = new QVBoxLayout(host);

    auto* bar = new QHBoxLayout;
    auto* addFiles = new QPushButton(style()->standardIcon(QStyle::SP_DialogOpenButton), "Add files…");
    auto* addLink = new QPushButton("Add from link…");
    addLink->setToolTip("Paste a MyInstants or Voicemod sound page, or a direct .mp3/.ogg/.wav link.");
    auto* stop = new QPushButton(style()->standardIcon(QStyle::SP_MediaStop), "Stop all");
    auto* prev = new QToolButton; prev->setArrowType(Qt::LeftArrow);
    auto* next = new QToolButton; next->setArrowType(Qt::RightArrow);
    bankLabel_ = new QLabel(QString("Bank %1").arg(app_.bindings().bank() + 1));
    bankLabel_->setToolTip("Banks let one pad/controller reach more sounds.\n"
                           "Bindings set to a bank only work while that bank is active.");
    auto* bindings = new QPushButton("Bindings…");
    auto* devices = new QPushButton("Input devices…");
    bar->addWidget(addFiles);
    bar->addWidget(addLink);
    bar->addWidget(stop);
    bar->addSpacing(12);
    bar->addWidget(prev);
    bar->addWidget(bankLabel_);
    bar->addWidget(next);
    bar->addStretch();
    bar->addWidget(bindings);
    bar->addWidget(devices);
    lay->addLayout(bar);

    search_ = new QLineEdit;
    search_->setPlaceholderText("Search sounds…");
    search_->setClearButtonEnabled(true);
    lay->addWidget(search_);

    padHost_ = new QWidget;
    padGrid_ = new QGridLayout(padHost_);
    padGrid_->setSpacing(8);
    padGrid_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    auto* scroll = new QScrollArea;
    scroll->setWidget(padHost_);
    scroll->setWidgetResizable(true);
    lay->addWidget(scroll, 1);

    connect(addFiles, &QPushButton::clicked, this, [this] {
        QStringList files = QFileDialog::getOpenFileNames(
            this, "Add sounds", QString(), "Audio (*.mp3 *.ogg *.oga *.opus *.wav *.flac *.m4a);;All files (*)");
        if (!files.isEmpty()) app_.importFiles(files);
    });
    connect(addLink, &QPushButton::clicked, this, &MainWindow::importLinkDialog);
    connect(stop, &QPushButton::clicked, this, [this] { app_.stopAll(); });
    auto shiftBank = [this](int d) {
        app_.bindings().setBank(app_.bindings().bank() + d);
        emit app_.bankChanged(app_.bindings().bank());
    };
    connect(prev, &QToolButton::clicked, this, [shiftBank] { shiftBank(-1); });
    connect(next, &QToolButton::clicked, this, [shiftBank] { shiftBank(+1); });
    connect(bindings, &QPushButton::clicked, this, [this] { BindingsDialog(app_, this).exec(); });
    connect(devices, &QPushButton::clicked, this, [this] { DevicesDialog(app_, this).exec(); });
    connect(search_, &QLineEdit::textChanged, this, &MainWindow::rebuildPads);
    return host;
}

void MainWindow::importLinkDialog() {
    bool ok = false;
    QString url = QInputDialog::getText(this, "Add from link",
                                        "Sound page or audio link (MyInstants, Voicemod, …):",
                                        QLineEdit::Normal, QString(), &ok);
    if (ok && !url.trimmed().isEmpty()) app_.importLink(url.trimmed());
}

void MainWindow::rebuildPads() {
    while (QLayoutItem* it = padGrid_->takeAt(0)) {
        delete it->widget();
        delete it;
    }
    const QString filter = search_->text().trimmed();
    const int bank = app_.bindings().bank();
    int n = 0;
    for (const SoundEntry& e : app_.library().sounds()) {
        if (!filter.isEmpty() && !e.title.contains(filter, Qt::CaseInsensitive)) continue;
        auto* pad = new QToolButton;
        pad->setFixedSize(170, 74);
        pad->setToolButtonStyle(Qt::ToolButtonTextOnly);

        // Show the binding active in this bank (bank-specific first, then global).
        QString keys;
        for (const Binding& b : app_.bindings().forSound(e.id.toStdString()))
            if (b.bank == bank || (b.bank == -1 && keys.isEmpty()))
                keys = QString::fromStdString(modsToString(b.mods) + (b.controlLabel.empty() ? b.control : b.controlLabel));
        QString state = app_.loadState(e.id);
        QString title = pad->fontMetrics().elidedText(e.title, Qt::ElideRight, 155);
        QString mode = e.mode == PlayMode::Hold ? " · hold" : e.mode == PlayMode::ToggleLoop ? " · loop"
                     : e.mode == PlayMode::Overlap ? " · layer" : "";
        pad->setText(title + "\n" + (state.isEmpty() ? (keys.isEmpty() ? QString("—") : keys) + mode : state));
        QString tip = e.title + "\n" + app_.clipInfo(e.id);
        if (!e.sourceUrl.isEmpty()) tip += "\nFrom: " + e.sourceUrl;
        if (!state.isEmpty()) tip += "\n" + state;
        pad->setToolTip(tip);
        if (!state.isEmpty() && state != "loading…") pad->setStyleSheet("color: " + theme::css(theme::error()) + ";");

        const QString id = e.id;
        if (e.mode == PlayMode::Hold) {
            connect(pad, &QToolButton::pressed, this, [this, id] { app_.pressSound(id); });
            connect(pad, &QToolButton::released, this, [this, id] { app_.releaseSound(id); });
        } else {
            connect(pad, &QToolButton::pressed, this, [this, id] { app_.pressSound(id); });
        }
        pad->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(pad, &QToolButton::customContextMenuRequested, this,
                [this, id, pad](const QPoint& pos) { padMenu(id, pad, pos); });
        padGrid_->addWidget(pad, n / kPadCols, n % kPadCols);
        ++n;
    }
    if (n == 0) {
        auto* empty = new QLabel(filter.isEmpty()
            ? "No sounds yet.\n\nDrop audio files here, use “Add files…”, or paste a MyInstants /\n"
              "Voicemod sound page with “Add from link…”."
            : "No sounds match.");
        empty->setAlignment(Qt::AlignCenter);
        padGrid_->addWidget(empty, 0, 0);
    }
}

void MainWindow::padMenu(const QString& id, QToolButton* pad, const QPoint& pos) {
    SoundEntry* e = app_.library().find(id);
    if (!e) return;
    QMenu menu;
    menu.addAction("Bind key / pad / button…", this, [this, id] { bindSound(id); });
    auto bound = app_.bindings().forSound(id.toStdString());
    if (!bound.empty())
        menu.addAction(QString("Remove %1 binding(s)").arg(bound.size()), this, [this, id] {
            app_.bindings().removeForSound(id.toStdString());
            app_.saveLibrary();
            rebuildPads();
        });
    QMenu* modes = menu.addMenu("Play mode");
    const std::pair<PlayMode, const char*> modeList[] = {
        {PlayMode::Restart, "Play once (press again restarts)"},
        {PlayMode::Overlap, "Play once (presses layer)"},
        {PlayMode::Hold, "Play while held"},
        {PlayMode::ToggleLoop, "Loop until pressed again"}};
    for (auto& [m, label] : modeList) {
        QAction* a = modes->addAction(label, this, [this, id, m = m] { app_.setMode(id, m); });
        a->setCheckable(true);
        a->setChecked(e->mode == m);
    }
    menu.addAction(QString("Volume trim (%1 dB)…").arg(double(e->trimDb), 0, 'f', 0), this, [this, id] {
        SoundEntry* s = app_.library().find(id);
        if (!s) return;
        bool ok = false;
        int v = QInputDialog::getInt(this, "Volume trim", "Trim for \"" + s->title + "\" (dB):",
                                     int(s->trimDb), -24, 12, 1, &ok);
        if (ok) app_.setTrim(id, float(v));
    });
    menu.addAction("Rename…", this, [this, id] {
        SoundEntry* s = app_.library().find(id);
        if (!s) return;
        bool ok = false;
        QString t = QInputDialog::getText(this, "Rename", "Name:", QLineEdit::Normal, s->title, &ok);
        if (ok && !t.trimmed().isEmpty()) { s->title = t.trimmed(); app_.saveLibrary(); rebuildPads(); }
    });
    if (!e->sourceUrl.isEmpty())
        menu.addAction("Open source page", this, [url = e->sourceUrl] { QDesktopServices::openUrl(QUrl(url)); });
    menu.addSeparator();
    menu.addAction("Remove sound", this, [this, id] { app_.removeSound(id); });
    menu.exec(pad->mapToGlobal(pos));
}

void MainWindow::bindSound(const QString& id) {
    SoundEntry* e = app_.library().find(id);
    if (!e) return;
    BindDialog d(app_, id, e->title, this);
    if (d.exec() == QDialog::Accepted) {
        app_.bindings().add(d.result());
        app_.saveLibrary();
        rebuildPads();
    }
}

// ============================================================================ window / tray

void MainWindow::buildTray() {
    if (!QSystemTrayIcon::isSystemTrayAvailable()) return;
    tray_ = new QSystemTrayIcon(QIcon::fromTheme("audio-input-microphone",
                                                 style()->standardIcon(QStyle::SP_MediaVolume)), this);
    tray_->setToolTip("voxfx");
    auto* menu = new QMenu(this);
    menu->addAction("Show voxfx", this, [this] { show(); raise(); activateWindow(); });
    trayMute_ = menu->addAction("Mute mic");
    trayMute_->setCheckable(true);
    connect(trayMute_, &QAction::toggled, this, [this](bool on) { app_.engine().params().micMute = on; syncFromParams(); });
    trayFx_ = menu->addAction("Voice effect");
    trayFx_->setCheckable(true);
    connect(trayFx_, &QAction::toggled, this, [this](bool on) { app_.engine().params().fxOn = on; syncFromParams(); });
    menu->addAction("Stop all sounds", this, [this] { app_.stopAll(); });
    menu->addAction("Release grabbed devices", this, [this] { app_.setGrabbedDevices({}); });
    menu->addSeparator();
    menu->addAction("Quit", this, [this] { quitting_ = true; close(); });
    tray_->setContextMenu(menu);
    connect(tray_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason r) {
        if (r == QSystemTrayIcon::Trigger) setVisible(!isVisible());
    });
    tray_->show();
}

void MainWindow::closeEvent(QCloseEvent* e) {
    if (tray_ && tray_->isVisible() && !quitting_) {
        hide(); // keep running in the tray so bindings keep working
        tray_->showMessage("voxfx", "Still running in the tray. Quit from the tray menu.",
                           QSystemTrayIcon::Information, 2500);
        e->ignore();
        return;
    }
    e->accept();
    qApp->quit();
}

void MainWindow::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* e) {
    QStringList files;
    for (const QUrl& u : e->mimeData()->urls()) {
        if (u.isLocalFile()) files << u.toLocalFile();
        else app_.importLink(u.toString()); // dragged a link from the browser
    }
    if (!files.isEmpty()) app_.importFiles(files);
}

} // namespace vfx
