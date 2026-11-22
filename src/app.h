#pragma once
// Application core: owns the engine, library, input backends and the importer,
// and wires bindings to actions. Lives on the Qt GUI thread; the binding
// dispatcher runs on input threads and only touches thread-safe state.
#include "common/settings.h"
#include "engine/engine.h"
#include "input/binding_manager.h"
#include "input/evdev_backend.h"
#include "input/midi_backend.h"
#include "sources/library.h"
#include "sources/link_importer.h"

#include <QLocalServer>
#include <QObject>
#include <QSettings>
#include <QTimer>

#include <map>
#include <memory>
#include <mutex>

namespace vfx {

class App : public QObject {
    Q_OBJECT
public:
    static constexpr const char* kCtlSocket = "voxfx-ctl";

    explicit App(QObject* parent = nullptr);
    ~App() override;

    bool start(QString* error);
    void shutdown();

    Engine& engine() { return engine_; }
    Library& library() { return library_; }
    BindingManager& bindings() { return bindings_; }
    EvdevBackend& evdev() { return evdev_; }
    MidiBackend& midi() { return midi_; }
    AppSettings& settings() { return settings_; }
    QString midiError() const { return midiError_; }

    // ---- sounds (GUI thread) ------------------------------------------------
    void importFiles(const QStringList& paths);
    void importLink(const QString& url);
    void removeSound(const QString& id);
    void setTrim(const QString& id, float db);
    void setMode(const QString& id, PlayMode m);
    void pressSound(const QString& id, float gain = 1.f);
    void releaseSound(const QString& id);
    void stopAll();
    // "" when loaded fine, otherwise the load error (or "loading…").
    QString loadState(const QString& id) const;
    QString clipInfo(const QString& id) const;

    // ---- persistence ----------------------------------------------------------
    void saveLibrary();
    void saveSettings();
    void setGrabbedDevices(const QStringList& keys);
    void setMicrophone(const QString& nodeName);

signals:
    void soundsChanged();
    void bindingsChanged();
    void paramsChanged();      // something other than the UI changed a parameter
    void bankChanged(int bank);
    void message(const QString& text);
    void showWindowRequested();

private:
    struct Loaded {
        std::unique_ptr<Clip> clip;
        int slot = -1;
        bool loading = false;
        QString error;
    };
    void loadSound(const SoundEntry& e);
    void publish(const QString& id, Clip* clip, const QString& error);
    void retire(const QString& id);
    void reap();
    void dispatch(const Binding& b, const InputEvent& ev);
    void handleCtl(const QString& line, QString& reply);
    QString addFileToLibrary(const QString& path, const QString& title, const QString& source,
                             const QString& sourceUrl, bool copyIntoLibrary);

    Engine engine_;
    Library library_;
    BindingManager bindings_;
    EvdevBackend evdev_;
    MidiBackend midi_;
    LinkImporter importer_;
    QSettings qsettings_;
    AppSettings settings_;
    QString midiError_;
    bool engineRunning_ = false;

    std::map<QString, Loaded> clips_;          // GUI thread only
    std::vector<int> freeSlots_;
    struct Grave { std::unique_ptr<Clip> clip; int slot; uint64_t freeAfter; };
    std::vector<Grave> graveyard_;
    QTimer reaper_;

    // Read by input threads in dispatch().
    struct SlotInfo { int slot; PlayMode mode; };
    mutable std::mutex slotMx_;
    std::map<std::string, SlotInfo> slotById_;

    QLocalServer ctl_;
};

} // namespace vfx
