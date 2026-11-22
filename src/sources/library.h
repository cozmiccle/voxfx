#pragma once
// The sound library: metadata for every imported sound plus the bindings, stored
// as one JSON file (~/.local/share/voxfx/library.json). Audio files live in
// ~/.local/share/voxfx/sounds/.
#include "common/types.h"

#include <QJsonArray>
#include <QString>
#include <vector>

namespace vfx {

struct SoundEntry {
    QString id;
    QString title;
    QString file;      // absolute path
    QString source;    // "local", "myinstants", "voicemod", "web"
    QString sourceUrl;
    float trimDb = 0.f;
    PlayMode mode = PlayMode::Restart;
};

const char* playModeName(PlayMode m);
PlayMode playModeFromName(const QString& s);

class Library {
public:
    explicit Library(QString dataDir);

    bool load(QString* error = nullptr);
    bool save(QString* error = nullptr) const;

    QString dataDir() const { return dir_; }
    QString soundsDir() const;
    const std::vector<SoundEntry>& sounds() const { return sounds_; }
    SoundEntry* find(const QString& id);
    const SoundEntry* findByTitle(const QString& title) const;
    QString add(SoundEntry e);   // assigns an id, returns it
    void remove(const QString& id, bool deleteFile);

    QJsonArray bindingsJson;     // owned by BindingManager, persisted here

private:
    QString dir_;
    std::vector<SoundEntry> sounds_;
};

} // namespace vfx
