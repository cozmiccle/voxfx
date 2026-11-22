#pragma once
// MIDI input via the ALSA sequencer (works on PipeWire systems too). Auto-connects
// to every MIDI controller, including ones plugged in later.
//   Note on/off  -> press/release, velocity -> InputEvent::value
//   CC >= 64     -> press, CC < 64 -> release (pads/buttons that send CCs)
#include "input/input_event.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

typedef struct _snd_seq snd_seq_t;

namespace vfx {

class MidiBackend {
public:
    using Sink = std::function<void(const InputEvent&)>;
    explicit MidiBackend(Sink sink);
    ~MidiBackend();

    bool start(std::string* error);
    void stop();
    std::vector<std::string> connectedDevices() const;

private:
    void run();
    void connectAll();
    void connectPort(int client, int port);
    std::string clientName(int client);

    Sink sink_;
    snd_seq_t* seq_ = nullptr;
    int myPort_ = -1;
    std::atomic<bool> running_{false};
    std::thread thread_;
    mutable std::mutex m_;
    std::vector<std::string> connected_;
};

} // namespace vfx
