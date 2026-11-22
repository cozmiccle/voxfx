#include "sources/library.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>

namespace vfx {

const char* playModeName(PlayMode m) {
    switch (m) {
        case PlayMode::Restart: return "restart";
        case PlayMode::Overlap: return "overlap";
        case PlayMode::Hold: return "hold";
        case PlayMode::ToggleLoop: return "loop";
    }
    return "restart";
}

PlayMode playModeFromName(const QString& s) {
    if (s == "overlap") return PlayMode::Overlap;
    if (s == "hold") return PlayMode::Hold;
    if (s == "loop") return PlayMode::ToggleLoop;
    return PlayMode::Restart;
}

Library::Library(QString dataDir) : dir_(std::move(dataDir)) {
    QDir().mkpath(soundsDir());
}

QString Library::soundsDir() const { return QDir(dir_).filePath("sounds"); }

bool Library::load(QString* error) {
    QFile f(QDir(dir_).filePath("library.json"));
    if (!f.exists()) return true;
    if (!f.open(QIODevice::ReadOnly)) { if (error) *error = f.errorString(); return false; }
    QJsonParseError pe;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError) { if (error) *error = pe.errorString(); return false; }
    QJsonObject root = doc.object();
    sounds_.clear();
    for (const auto& v : root["sounds"].toArray()) {
        QJsonObject o = v.toObject();
        SoundEntry e;
        e.id = o["id"].toString();
        e.title = o["title"].toString();
        e.file = o["file"].toString();
        if (QDir::isRelativePath(e.file)) e.file = QDir(soundsDir()).filePath(e.file);
        e.source = o["source"].toString("local");
        e.sourceUrl = o["sourceUrl"].toString();
        e.trimDb = float(o["trimDb"].toDouble(0));
        e.mode = playModeFromName(o["mode"].toString());
        if (!e.id.isEmpty()) sounds_.push_back(e);
    }
    bindingsJson = root["bindings"].toArray();
    return true;
}

bool Library::save(QString* error) const {
    QJsonArray arr;
    for (const auto& e : sounds_) {
        QJsonObject o;
        o["id"] = e.id;
        o["title"] = e.title;
        // Store files inside our sounds dir relative, so the folder can be moved.
        QString rel = QDir(soundsDir()).relativeFilePath(e.file);
        o["file"] = rel.startsWith("..") ? e.file : rel;
        o["source"] = e.source;
        o["sourceUrl"] = e.sourceUrl;
        o["trimDb"] = double(e.trimDb);
        o["mode"] = playModeName(e.mode);
        arr.append(o);
    }
    QJsonObject root;
    root["version"] = 1;
    root["sounds"] = arr;
    root["bindings"] = bindingsJson;
    QSaveFile f(QDir(dir_).filePath("library.json"));
    if (!f.open(QIODevice::WriteOnly)) { if (error) *error = f.errorString(); return false; }
    f.write(QJsonDocument(root).toJson());
    if (!f.commit()) { if (error) *error = f.errorString(); return false; }
    return true;
}

SoundEntry* Library::find(const QString& id) {
    for (auto& e : sounds_) if (e.id == id) return &e;
    return nullptr;
}

const SoundEntry* Library::findByTitle(const QString& title) const {
    for (auto& e : sounds_) if (e.title.compare(title, Qt::CaseInsensitive) == 0) return &e;
    return nullptr;
}

QString Library::add(SoundEntry e) {
    e.id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    sounds_.push_back(e);
    return e.id;
}

void Library::remove(const QString& id, bool deleteFile) {
    for (auto it = sounds_.begin(); it != sounds_.end(); ++it) {
        if (it->id != id) continue;
        // Only delete files we own (inside our sounds dir).
        if (deleteFile && it->file.startsWith(soundsDir())) QFile::remove(it->file);
        sounds_.erase(it);
        return;
    }
}

} // namespace vfx
