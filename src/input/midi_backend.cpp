#include "input/midi_backend.h"

#include <alsa/asoundlib.h>

#include <algorithm>
#include <poll.h>

namespace vfx {

MidiBackend::MidiBackend(Sink sink) : sink_(std::move(sink)) {}
MidiBackend::~MidiBackend() { stop(); }

bool MidiBackend::start(std::string* error) {
    if (running_) return true;
    if (snd_seq_open(&seq_, "default", SND_SEQ_OPEN_INPUT, SND_SEQ_NONBLOCK) < 0) {
        seq_ = nullptr;
        if (error) *error = "ALSA sequencer not available (no /dev/snd/seq?)";
        return false;
    }
    snd_seq_set_client_name(seq_, "voxfx");
    myPort_ = snd_seq_create_simple_port(seq_, "voxfx input",
                                         SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
                                         SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
    // Get told about new ports (hotplug).
    snd_seq_connect_from(seq_, myPort_, SND_SEQ_CLIENT_SYSTEM, SND_SEQ_PORT_SYSTEM_ANNOUNCE);
    connectAll();
    running_ = true;
    thread_ = std::thread([this] { run(); });
    return true;
}

void MidiBackend::stop() {
    if (running_.exchange(false) && thread_.joinable()) thread_.join();
    if (seq_) { snd_seq_close(seq_); seq_ = nullptr; }
}

std::vector<std::string> MidiBackend::connectedDevices() const {
    std::lock_guard lk(m_);
    return connected_;
}

std::string MidiBackend::clientName(int client) {
    snd_seq_client_info_t* ci;
    snd_seq_client_info_alloca(&ci);
    if (snd_seq_get_any_client_info(seq_, client, ci) < 0) return "client " + std::to_string(client);
    return snd_seq_client_info_get_name(ci);
}

void MidiBackend::connectPort(int client, int port) {
    if (client == SND_SEQ_CLIENT_SYSTEM || client == snd_seq_client_id(seq_)) return;
    snd_seq_port_info_t* pi;
    snd_seq_port_info_alloca(&pi);
    if (snd_seq_get_any_port_info(seq_, client, port, pi) < 0) return;
    unsigned caps = snd_seq_port_info_get_capability(pi);
    unsigned type = snd_seq_port_info_get_type(pi);
    if ((caps & (SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ)) !=
        (SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ)) return;
    if (!(type & SND_SEQ_PORT_TYPE_MIDI_GENERIC)) return;
    std::string name = clientName(client);
    if (name == "Midi Through") return;
    if (snd_seq_connect_from(seq_, myPort_, client, port) == 0) {
        std::lock_guard lk(m_);
        if (std::find(connected_.begin(), connected_.end(), name) == connected_.end())
            connected_.push_back(name);
    }
}

void MidiBackend::connectAll() {
    snd_seq_client_info_t* ci;
    snd_seq_port_info_t* pi;
    snd_seq_client_info_alloca(&ci);
    snd_seq_port_info_alloca(&pi);
    snd_seq_client_info_set_client(ci, -1);
    while (snd_seq_query_next_client(seq_, ci) >= 0) {
        int client = snd_seq_client_info_get_client(ci);
        snd_seq_port_info_set_client(pi, client);
        snd_seq_port_info_set_port(pi, -1);
        while (snd_seq_query_next_port(seq_, pi) >= 0)
            connectPort(client, snd_seq_port_info_get_port(pi));
    }
}

void MidiBackend::run() {
    int nfds = snd_seq_poll_descriptors_count(seq_, POLLIN);
    std::vector<pollfd> pfds(size_t(std::max(nfds, 1)));
    snd_seq_poll_descriptors(seq_, pfds.data(), unsigned(nfds), POLLIN);
    while (running_) {
        if (poll(pfds.data(), pfds.size(), 250) <= 0) continue;
        snd_seq_event_t* ev = nullptr;
        while (snd_seq_event_input(seq_, &ev) >= 0 && ev) {
            InputEvent ie;
            const int src = ev->source.client;
            switch (ev->type) {
                case SND_SEQ_EVENT_PORT_START:
                    connectPort(ev->data.addr.client, ev->data.addr.port);
                    continue;
                case SND_SEQ_EVENT_NOTEON:
                case SND_SEQ_EVENT_NOTEOFF: {
                    const auto& n = ev->data.note;
                    ie.control = "note:" + std::to_string(n.channel) + ":" + std::to_string(n.note);
                    ie.label = "Note " + std::to_string(n.note) + " (ch " + std::to_string(n.channel + 1) + ")";
                    ie.pressed = ev->type == SND_SEQ_EVENT_NOTEON && n.velocity > 0;
                    ie.value = ie.pressed ? n.velocity / 127.f : 0.f;
                    break;
                }
                case SND_SEQ_EVENT_CONTROLLER: {
                    const auto& c = ev->data.control;
                    ie.control = "cc:" + std::to_string(c.channel) + ":" + std::to_string(c.param);
                    ie.label = "CC " + std::to_string(c.param) + " (ch " + std::to_string(c.channel + 1) + ")";
                    ie.pressed = c.value >= 64;
                    ie.value = 1.f;
                    break;
                }
                default:
                    continue;
            }
            std::string name = clientName(src);
            ie.device = "midi:" + name;
            ie.deviceLabel = name;
            sink_(ie);
        }
    }
}

} // namespace vfx
