#include "app.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocalSocket>
#include <QStandardPaths>
#include <QDateTime>
#include <QImage>
#include <QImageReader>
#include <QThreadPool>

namespace vfx {

namespace {
// ~/.local/share/voxfx (AppDataLocation would add the org name twice).
QString dataDir() {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)).filePath("voxfx");
}
} // namespace

App::App(QObject* parent)
    : QObject(parent),
      library_(dataDir()),
      evdev_([this](const InputEvent& e) { bindings_.handle(e); }),
      midi_([this](const InputEvent& e) { bindings_.handle(e); }),
      importer_(QDir(dataDir()).filePath("sounds")),
      qsettings_("voxfx", "voxfx"),
      recorder_(engine_) {
    // The last slot is reserved for previewing files that aren't in the library.
    for (int i = kMaxClipSlots - 2; i >= 0; --i) freeSlots_.push_back(i);
    bindings_.setDispatcher([this](const Binding& b, const InputEvent& e) { dispatch(b, e); });

    connect(&reaper_, &QTimer::timeout, this, &App::reap);
    reaper_.start(100);

    connect(&importer_, &LinkImporter::progress, this, &App::message);
    connect(&importer_, &LinkImporter::failed, this, [this](const QString& url, const QString& err) {
        emit message("Import failed: " + err + " (" + url + ")");
    });
    connect(&importer_, &LinkImporter::imported, this,
            [this](const QString& path, const QString& title, const QString& source, const QString& url) {
                addFileToLibrary(path, title, source, url, false);
                emit message("Imported \"" + title + "\"");
            });
}

App::~App() { shutdown(); }

bool App::start(QString* error) {
    QString libErr;
    if (!library_.load(&libErr)) emit message("Library could not be read: " + libErr);
    bindings_.fromJson(library_.bindingsJson);
    loadSettings(qsettings_, engine_.params(), settings_);

    std::string err;
    engineRunning_ = engine_.start(&err);
    if (!engineRunning_) {
        if (error) *error = QString::fromStdString(err);
        return false;
    }
    engine_.selectMicrophone(settings_.micName.toStdString());
    engine_.setMonitor(engine_.params().monitorOn.load());

    std::set<std::string> grabs;
    for (auto& k : settings_.grabbedDevices) grabs.insert(k.toStdString());
    evdev_.setGrabbed(grabs);
    evdev_.start();
    std::string merr;
    if (!midi_.start(&merr)) midiError_ = QString::fromStdString(merr);

    for (const auto& e : library_.sounds()) loadSound(e);

    // Control socket for `voxfx ctl ...` (compositor key bindings, scripts).
    QLocalServer::removeServer(kCtlSocket);
    ctl_.setSocketOptions(QLocalServer::UserAccessOption);
    if (ctl_.listen(kCtlSocket)) {
        connect(&ctl_, &QLocalServer::newConnection, this, [this] {
            while (QLocalSocket* s = ctl_.nextPendingConnection()) {
                connect(s, &QLocalSocket::readyRead, s, [this, s] {
                    while (s->canReadLine()) {
                        QString reply;
                        handleCtl(QString::fromUtf8(s->readLine()).trimmed(), reply);
                        s->write((reply + "\n").toUtf8());
                    }
                    s->flush();
                });
                connect(s, &QLocalSocket::disconnected, s, &QObject::deleteLater);
            }
        });
    }
    return true;
}

void App::shutdown() {
    if (!engineRunning_) return;
    saveSettings();
    saveLibrary();
    ctl_.close();
    if (recorder_.active()) {
        recorder_.stop();
        QFile::remove(recPath_); // unsaved take
    }
    midi_.stop();
    evdev_.stop();
    QThreadPool::globalInstance()->waitForDone(5000);
    engine_.stop();
    engineRunning_ = false;
    for (auto& [id, l] : clips_) if (l.slot >= 0) engine_.bank().entries[l.slot].store(nullptr);
    clips_.clear();
    graveyard_.clear();
}

// ============================================================================ sounds

QString App::addFileToLibrary(const QString& path, const QString& title, const QString& source,
                              const QString& sourceUrl, bool copyIntoLibrary) {
    SoundEntry e;
    e.title = title;
    e.source = source;
    e.sourceUrl = sourceUrl;
    e.file = path;
    if (copyIntoLibrary) {
        QDir dir(library_.soundsDir());
        QFileInfo fi(path);
        QString dest = dir.filePath(fi.fileName());
        for (int i = 2; QFile::exists(dest); ++i)
            dest = dir.filePath(fi.completeBaseName() + QString("_%1.").arg(i) + fi.suffix());
        if (QFile::copy(path, dest)) e.file = dest;
    }
    QString id = library_.add(e);
    saveLibrary();
    if (SoundEntry* added = library_.find(id)) loadSound(*added);
    emit soundsChanged();
    return id;
}

void App::importFiles(const QStringList& paths) {
    for (const QString& p : paths)
        addFileToLibrary(p, QFileInfo(p).completeBaseName(), "local", QString(), true);
}

void App::importLink(const QString& url) { importer_.import(url); }

void App::loadSound(const SoundEntry& e) {
    auto& l = clips_[e.id];
    l.loading = true;
    l.error.clear();
    const QString id = e.id, file = e.file;
    QThreadPool::globalInstance()->start([this, id, file] {
        auto* clip = new Clip;
        LoadResult r = loadClip(file.toStdString(), *clip);
        QString err = r.ok ? QString() : QString::fromStdString(r.error);
        if (!r.ok) { delete clip; clip = nullptr; }
        QMetaObject::invokeMethod(this, [this, id, clip, err] { publish(id, clip, err); },
                                  Qt::QueuedConnection);
    });
}

void App::publish(const QString& id, Clip* clipRaw, const QString& error) {
    std::unique_ptr<Clip> clip(clipRaw);
    SoundEntry* e = library_.find(id);
    if (!e) return; // removed while loading
    retire(id);     // replace any previous version
    auto& l = clips_[id];
    l.loading = false;
    l.error = error;
    if (clip && !freeSlots_.empty()) {
        clip->trimGain.store(dbToLin(e->trimDb));
        l.slot = freeSlots_.back();
        freeSlots_.pop_back();
        l.clip = std::move(clip);
        engine_.bank().entries[l.slot].store(l.clip.get(), std::memory_order_release);
        std::lock_guard lk(slotMx_);
        slotById_[id.toStdString()] = {l.slot, e->mode};
    } else if (clip) {
        l.error = "too many sounds loaded";
    }
    emit soundsChanged();
}

void App::retire(const QString& id) {
    auto it = clips_.find(id);
    if (it == clips_.end() || it->second.slot < 0) return;
    {
        std::lock_guard lk(slotMx_);
        slotById_.erase(id.toStdString());
    }
    engine_.bank().entries[it->second.slot].store(nullptr, std::memory_order_release);
    // The audio thread may still be inside a block that started before the store.
    // Two more completed blocks guarantee every voice has seen the null slot.
    uint64_t now = engine_.meters().cycles.load(std::memory_order_acquire);
    graveyard_.push_back({std::move(it->second.clip), it->second.slot, now + 2});
    it->second.slot = -1;
}

void App::reap() {
    uint64_t now = engine_.meters().cycles.load(std::memory_order_acquire);
    for (auto it = graveyard_.begin(); it != graveyard_.end();) {
        if (now >= it->freeAfter || !engineRunning_) {
            if (it->returnSlot) freeSlots_.push_back(it->slot);
            it = graveyard_.erase(it);
        } else {
            ++it;
        }
    }
}

void App::removeSound(const QString& id) {
    retire(id);
    clips_.erase(id);
    bindings_.removeForSound(id.toStdString());
    library_.remove(id, true);
    saveLibrary();
    emit soundsChanged();
    emit bindingsChanged();
}

void App::setTrim(const QString& id, float db) {
    SoundEntry* e = library_.find(id);
    if (!e) return;
    e->trimDb = db;
    auto it = clips_.find(id);
    if (it != clips_.end() && it->second.clip) it->second.clip->trimGain.store(dbToLin(db));
    saveLibrary();
}

void App::setMode(const QString& id, PlayMode m) {
    SoundEntry* e = library_.find(id);
    if (!e) return;
    e->mode = m;
    {
        std::lock_guard lk(slotMx_);
        auto it = slotById_.find(id.toStdString());
        if (it != slotById_.end()) it->second.mode = m;
    }
    saveLibrary();
    emit soundsChanged();
}

void App::pressSound(const QString& id, float gain) {
    std::lock_guard lk(slotMx_);
    auto it = slotById_.find(id.toStdString());
    if (it == slotById_.end()) return;
    engine_.send({Command::Play, it->second.mode, int16_t(it->second.slot), gain});
}

void App::releaseSound(const QString& id) {
    std::lock_guard lk(slotMx_);
    auto it = slotById_.find(id.toStdString());
    if (it == slotById_.end() || it->second.mode != PlayMode::Hold) return;
    engine_.send({Command::Release, PlayMode::Hold, int16_t(it->second.slot), 1.f});
}

void App::stopAll() { engine_.send({Command::StopAll}); }

QString App::loadState(const QString& id) const {
    auto it = clips_.find(id);
    if (it == clips_.end()) return "not loaded";
    if (it->second.loading) return "loading…";
    return it->second.error;
}

QString App::clipInfo(const QString& id) const {
    auto it = clips_.find(id);
    if (it == clips_.end() || !it->second.clip) return {};
    const Clip& c = *it->second.clip;
    return QString("%1 s · %2 LUFS → normalised %3 dB")
        .arg(double(c.frames()) / kClipRate, 0, 'f', 1)
        .arg(c.loudnessLufs, 0, 'f', 1)
        .arg(20.0 * std::log10(std::max(1e-6f, c.normGain)), 0, 'f', 1);
}

void App::previewSound(const QString& id) {
    std::lock_guard lk(slotMx_);
    auto it = slotById_.find(id.toStdString());
    if (it == slotById_.end()) return;
    Command c{Command::Play, PlayMode::Restart, int16_t(it->second.slot), 1.f};
    c.preview = true;
    engine_.send(c);
}

void App::rename(const QString& id, const QString& title) {
    SoundEntry* e = library_.find(id);
    if (!e || title.trimmed().isEmpty()) return;
    e->title = title.trimmed();
    saveLibrary();
    emit soundsChanged();
}

void App::setImage(const QString& id, const QString& imagePath) {
    SoundEntry* e = library_.find(id);
    if (!e) return;
    auto replace = [this, id](const QString& newPath) {
        SoundEntry* s = library_.find(id);
        if (!s) { QFile::remove(newPath); return; }
        if (!s->image.isEmpty() && s->image.startsWith(library_.imagesDir())) QFile::remove(s->image);
        s->image = newPath;
        saveLibrary();
        emit soundsChanged();
    };
    if (imagePath.isEmpty()) { replace(QString()); return; }
    // Decode, centre-crop to a square and scale to 256 px off the GUI thread, so
    // a huge photo never stalls the window.
    const QString dest = QDir(library_.imagesDir())
        .filePath(QString("%1-%2.png").arg(id).arg(QDateTime::currentMSecsSinceEpoch()));
    QThreadPool::globalInstance()->start([this, imagePath, dest, replace] {
        QImageReader r(imagePath);
        r.setAutoTransform(true);
        QSize sz = r.size();
        if (sz.isValid() && std::max(sz.width(), sz.height()) > 2048)
            r.setScaledSize(sz.scaled(2048, 2048, Qt::KeepAspectRatio));
        QImage img = r.read();
        bool ok = !img.isNull();
        if (ok) {
            int side = std::min(img.width(), img.height());
            img = img.copy((img.width() - side) / 2, (img.height() - side) / 2, side, side)
                      .scaled(256, 256, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            ok = img.save(dest, "PNG");
        }
        QString err = ok ? QString() : "Couldn't use that image: " + r.errorString();
        QMetaObject::invokeMethod(this, [this, ok, dest, err, replace] {
            if (ok) replace(dest);
            else emit message(err);
        }, Qt::QueuedConnection);
    });
}

float App::progress(const QString& id) const {
    auto it = clips_.find(id);
    if (it == clips_.end() || it->second.slot < 0) return -1.f;
    return engine_.slotProgress(it->second.slot);
}

// ============================================================================ recording

bool App::startRecording(QString* error) {
    if (recorder_.active()) return true;
    recPath_ = QDir(library_.soundsDir())
        .filePath("rec-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") + ".wav");
    std::string err;
    if (!recorder_.start(recPath_.toStdString(), &err)) {
        if (error) *error = QString::fromStdString(err);
        return false;
    }
    emit recordingChanged(true);
    return true;
}

QString App::stopRecording(QString* error, bool trim) {
    if (!recorder_.active()) { if (error) *error = "not recording"; return {}; }
    RecordingInfo info = recorder_.stop();
    emit recordingChanged(false);
    if (!info.ok) {
        QFile::remove(recPath_);
        if (error) *error = QString::fromStdString(info.error);
        return {};
    }
    if (info.dropped > 0) emit message(QString("Recording lost %1 ms of audio (system busy)").arg(info.dropped * 1000 / std::max(1u, engine_.meters().sampleRate.load())));
    if (trim) {
        std::string err;
        if (!trimSilence(recPath_.toStdString(), -45.f, 80.0, &err) && err == "recording is silent") {
            QFile::remove(recPath_);
            if (error) *error = "The recording is silent. Check the mic and the Mic meter, then try again.";
            return {};
        }
    }
    return recPath_;
}

void App::previewFile(const QString& path) {
    auto play = [this] {
        Command c{Command::Play, PlayMode::Restart, int16_t(kPreviewSlot), 1.f};
        c.preview = true;
        engine_.send(c);
    };
    if (path == previewPath_ && previewClip_) { play(); return; }
    // Retire the previous preview clip (slot stays reserved).
    if (previewClip_) {
        engine_.bank().entries[kPreviewSlot].store(nullptr, std::memory_order_release);
        graveyard_.push_back({std::move(previewClip_), kPreviewSlot,
                              engine_.meters().cycles.load() + 2, false});
    }
    previewPath_ = path;
    QThreadPool::globalInstance()->start([this, path, play] {
        auto* clip = new Clip;
        LoadResult r = loadClip(path.toStdString(), *clip);
        QMetaObject::invokeMethod(this, [this, path, clip, ok = r.ok, play] {
            std::unique_ptr<Clip> c(clip);
            if (!ok || path != previewPath_) return; // failed or superseded
            previewClip_ = std::move(c);
            engine_.bank().entries[kPreviewSlot].store(previewClip_.get(), std::memory_order_release);
            play();
        }, Qt::QueuedConnection);
    });
}

void App::discardRecording(const QString& wavPath) {
    if (wavPath == previewPath_) {
        engine_.bank().entries[kPreviewSlot].store(nullptr, std::memory_order_release);
        if (previewClip_)
            graveyard_.push_back({std::move(previewClip_), kPreviewSlot,
                                  engine_.meters().cycles.load() + 2, false});
        previewPath_.clear();
    }
    QFile::remove(wavPath);
}

QString App::addRecording(const QString& wavPath, const QString& title, const QString& imagePath) {
    QString id = addFileToLibrary(wavPath, title.trimmed().isEmpty() ? QString("Recording") : title.trimmed(),
                                  "recorded", QString(), false);
    if (!imagePath.isEmpty()) setImage(id, imagePath);
    return id;
}

// ============================================================================ input

void App::dispatch(const Binding& b, const InputEvent& ev) {
    // Runs on an input thread. Only touches atomics, the engine queue, slotById_
    // (mutex) and the BindingManager; UI updates go through queued signals.
    Params& p = engine_.params();
    switch (b.action) {
        case Action::PlaySound: {
            std::lock_guard lk(slotMx_);
            auto it = slotById_.find(b.soundId);
            if (it == slotById_.end()) return;
            const int16_t slot = int16_t(it->second.slot);
            if (ev.pressed) {
                float gain = (b.useVelocity && ev.value < 1.f) ? std::max(0.15f, ev.value) : 1.f;
                engine_.send({Command::Play, it->second.mode, slot, gain});
            } else if (it->second.mode == PlayMode::Hold) {
                engine_.send({Command::Release, PlayMode::Hold, slot, 1.f});
            }
            return;
        }
        case Action::PushToTalk:
            p.pttHeld.store(ev.pressed);
            break;
        case Action::StopAll:
            if (ev.pressed) engine_.send({Command::StopAll});
            return;
        case Action::ToggleMute:
            if (ev.pressed) p.micMute.store(!p.micMute.load());
            break;
        case Action::ToggleFx:
            if (ev.pressed) p.fxOn.store(!p.fxOn.load());
            break;
        case Action::NextPreset:
            if (ev.pressed) p.preset.store((p.preset.load() + 1) % int(VoicePreset::Count));
            break;
        case Action::BankNext:
        case Action::BankPrev:
            if (ev.pressed) {
                bindings_.setBank(bindings_.bank() + (b.action == Action::BankNext ? 1 : -1));
                emit bankChanged(bindings_.bank()); // queued to the GUI thread
            }
            return;
    }
    emit paramsChanged();
}

void App::handleCtl(const QString& line, QString& reply) {
    const QString cmd = line.section(' ', 0, 0).toLower();
    const QString arg = line.section(' ', 1).trimmed();
    Params& p = engine_.params();
    reply = "ok";
    if (cmd == "play") {
        const SoundEntry* e = library_.find(arg) ? library_.find(arg) : library_.findByTitle(arg);
        if (!e) { reply = "error: no sound named '" + arg + "'"; return; }
        pressSound(e->id);
    } else if (cmd == "stop") {
        stopAll();
    } else if (cmd == "mute") { p.micMute = true; emit paramsChanged(); }
    else if (cmd == "unmute") { p.micMute = false; emit paramsChanged(); }
    else if (cmd == "toggle-mute") { p.micMute = !p.micMute; emit paramsChanged(); }
    else if (cmd == "ptt-down") { p.pttHeld = true; }
    else if (cmd == "ptt-up") { p.pttHeld = false; }
    else if (cmd == "preset") {
        for (int i = 0; i < int(VoicePreset::Count); ++i)
            if (arg.compare(presetName(VoicePreset(i)), Qt::CaseInsensitive) == 0) {
                p.preset = i; emit paramsChanged(); return;
            }
        reply = "error: unknown preset";
    } else if (cmd == "bank") {
        bindings_.setBank(arg.toInt() - 1);
        emit bankChanged(bindings_.bank());
    } else if (cmd == "list") {
        QStringList names;
        for (auto& e : library_.sounds()) names << e.title;
        reply = names.join("\n");
    } else if (cmd == "record-start") {
        QString err;
        if (!startRecording(&err)) reply = "error: " + err;
    } else if (cmd == "record-stop") {
        // record-stop [name]: saves the take into the library.
        QString err;
        QString path = stopRecording(&err);
        if (path.isEmpty()) { reply = "error: " + err; return; }
        QString title = arg.isEmpty() ? QString("Recording %1").arg(library_.sounds().size() + 1) : arg;
        addRecording(path, title, QString());
        reply = "ok " + title;
    } else if (cmd == "dev-screenshot" && qEnvironmentVariableIsSet("VOXFX_DEV")) {
        emit devScreenshotRequested(arg);
    } else if (cmd == "show") {
        emit showWindowRequested();
    } else {
        reply = "error: commands are play <name>, stop, mute, unmute, toggle-mute, ptt-down, "
                "ptt-up, preset <name>, bank <n>, list, show";
    }
}

// ============================================================================ persistence

void App::saveLibrary() {
    library_.bindingsJson = bindings_.toJson();
    QString err;
    if (!library_.save(&err)) emit message("Could not save library: " + err);
}

void App::saveSettings() {
    vfx::saveSettings(qsettings_, engine_.params(), settings_);
    qsettings_.sync();
}

void App::setGrabbedDevices(const QStringList& keys) {
    settings_.grabbedDevices = keys;
    std::set<std::string> s;
    for (auto& k : keys) s.insert(k.toStdString());
    evdev_.setGrabbed(s);
    saveSettings();
}

void App::setMicrophone(const QString& nodeName) {
    settings_.micName = nodeName;
    engine_.selectMicrophone(nodeName.toStdString());
    saveSettings();
}

} // namespace vfx
