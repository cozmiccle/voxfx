#include "engine/engine.h"

#include "dsp/dsp.h"
#include "dsp/voice_chain.h"
#include "sfx/voice_pool.h"

#include <pipewire/extensions/metadata.h>
#include <pipewire/filter.h>
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/param.h>
#include <spa/pod/builder.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <set>

namespace vfx {

namespace {

struct NodeInfo {
    std::string name, description, mediaClass;
};
struct PortInfo {
    uint32_t node = SPA_ID_INVALID;
    std::string name, channel;
    bool output = false;
    bool monitor = false;
};

// Extract "name" from metadata JSON like {"name":"alsa_output.foo"}.
std::string jsonName(const char* v) {
    if (!v) return {};
    std::string s(v);
    auto k = s.find("\"name\"");
    if (k == std::string::npos) return {};
    auto a = s.find('"', s.find(':', k) + 1);
    if (a == std::string::npos) return {};
    auto b = s.find('"', a + 1);
    return b == std::string::npos ? std::string{} : s.substr(a + 1, b - a - 1);
}

const char* prop(const spa_dict* d, const char* k) {
    const char* v = d ? spa_dict_lookup(d, k) : nullptr;
    return v ? v : "";
}

constexpr int kChunk = 1024; // internal processing block

// Put an adapter node (our virtual mic, the test tone) into DSP mode with the
// given channels. WirePlumber normally does this for every adapter node and
// must stay in charge when it's running (otherwise apps can't select the node),
// so this is only a fallback used when no session manager configured the ports
// within a moment of creating the node. VOXFX_NO_PORTCONFIG=1 disables it.
void configureAdapterPorts(pw_proxy* node, spa_direction dir, bool monitor, int channels) {
    if (!node || getenv("VOXFX_NO_PORTCONFIG")) return;
    uint8_t fbuf[512], pbuf[1024];
    spa_pod_builder fb = SPA_POD_BUILDER_INIT(fbuf, sizeof(fbuf));
    spa_pod_builder pb = SPA_POD_BUILDER_INIT(pbuf, sizeof(pbuf));
    spa_audio_info_raw info{};
    info.format = SPA_AUDIO_FORMAT_F32P;
    info.rate = 48000;
    info.channels = uint32_t(channels);
    if (channels == 1) info.position[0] = SPA_AUDIO_CHANNEL_MONO;
    else { info.position[0] = SPA_AUDIO_CHANNEL_FL; info.position[1] = SPA_AUDIO_CHANNEL_FR; }
    spa_pod* fmt = spa_format_audio_raw_build(&fb, SPA_PARAM_Format, &info);
    auto* param = static_cast<spa_pod*>(spa_pod_builder_add_object(
        &pb, SPA_TYPE_OBJECT_ParamPortConfig, SPA_PARAM_PortConfig,
        SPA_PARAM_PORT_CONFIG_direction, SPA_POD_Id(dir),
        SPA_PARAM_PORT_CONFIG_mode, SPA_POD_Id(SPA_PARAM_PORT_CONFIG_MODE_dsp),
        SPA_PARAM_PORT_CONFIG_monitor, SPA_POD_Bool(monitor),
        SPA_PARAM_PORT_CONFIG_format, SPA_POD_Pod(fmt)));
    pw_node_set_param(reinterpret_cast<pw_node*>(node), SPA_PARAM_PortConfig, 0, param);
}

void peakMax(std::atomic<float>& m, float v) {
    if (v > m.load(std::memory_order_relaxed)) m.store(v, std::memory_order_relaxed);
}

} // namespace

struct Port { int unused; };

struct Engine::Impl {
    // ---- shared state ----------------------------------------------------
    Params params;
    Meters meters;
    ClipBank bank;
    MpmcQueue<Command> queue{1024};

    // ---- PipeWire objects (touched only with the thread loop locked) -------
    pw_thread_loop* loop = nullptr;
    pw_context* context = nullptr;
    pw_core* core = nullptr;
    pw_registry* registry = nullptr;
    spa_hook coreListener{}, registryListener{}, filterListener{}, metadataListener{};
    pw_filter* filter = nullptr;
    Port* micIn = nullptr;
    Port* outVmic[2] = {};
    Port* outMon[2] = {};
    pw_proxy* vmicProxy = nullptr;
    pw_proxy* toneProxy = nullptr;
    spa_source* fallbackTimer = nullptr;
    pw_metadata* metadata = nullptr;

    std::map<uint32_t, NodeInfo> nodes;
    std::map<uint32_t, PortInfo> ports;
    std::map<std::pair<uint32_t, uint32_t>, pw_proxy*> links; // (outPort,inPort) -> proxy
    std::string defaultSource, defaultSink, selectedMic, linkedMic;
    bool monitorWanted = false;
    bool connected = false;
    std::function<void()> changed;

    // ---- RT-thread-only state --------------------------------------------
    dsp::VoiceChain chain;
    dsp::StereoLimiter limiter;
    dsp::EnvFollower duckEnv;
    VoicePool pool{bank};
    float rate = 0;
    float muteGain = 1.f, duckGain = 1.f, noiseMs = 0.f;
    float mic[kChunk], sfxL[kChunk], sfxR[kChunk];

    // =====================================================================
    // RT process callback
    // =====================================================================
    static void onProcess(void* data, spa_io_position* position) {
        static_cast<Impl*>(data)->process(position);
    }

    void process(spa_io_position* position) {
        const uint32_t n = position->clock.duration;
        const uint32_t r = position->clock.rate.denom ? position->clock.rate.denom : 48000;
        if (float(r) != rate) prepare(float(r)); // rare: graph rate change

        Command cmd;
        while (queue.pop(cmd)) pool.apply(cmd);

        auto* in = static_cast<float*>(pw_filter_get_dsp_buffer(micIn, n));
        float* vl = static_cast<float*>(pw_filter_get_dsp_buffer(outVmic[0], n));
        float* vr = static_cast<float*>(pw_filter_get_dsp_buffer(outVmic[1], n));
        float* ml = static_cast<float*>(pw_filter_get_dsp_buffer(outMon[0], n));
        float* mr = static_cast<float*>(pw_filter_get_dsp_buffer(outMon[1], n));

        dsp::ChainSettings cs;
        cs.cleanup = params.cleanupOn.load(std::memory_order_relaxed);
        cs.nsMix = params.nsMix.load(std::memory_order_relaxed);
        cs.gateThreshDb = params.gateThreshDb.load(std::memory_order_relaxed);
        cs.comp = params.compOn.load(std::memory_order_relaxed);
        cs.fx = params.fxOn.load(std::memory_order_relaxed);
        cs.preset = VoicePreset(std::clamp(params.preset.load(std::memory_order_relaxed), 0,
                                           int(VoicePreset::Count) - 1));
        const float micGain = dbToLin(params.micGainDb.load(std::memory_order_relaxed));
        const float sfxGain = dbToLin(params.sfxGainDb.load(std::memory_order_relaxed));
        const float duckDb = params.duckDb.load(std::memory_order_relaxed);
        const float duckLin = dbToLin(std::min(duckDb, 0.f));
        const bool limOn = params.limiterOn.load(std::memory_order_relaxed);
        const bool monOn = params.monitorOn.load(std::memory_order_relaxed);
        const bool monMic = params.monitorMic.load(std::memory_order_relaxed);
        const float monGain = dbToLin(params.monitorGainDb.load(std::memory_order_relaxed));
        const bool silenced = params.micMute.load(std::memory_order_relaxed) ||
            (params.pttEnabled.load(std::memory_order_relaxed) &&
             !params.pttHeld.load(std::memory_order_relaxed));
        const float muteTarget = silenced ? 0.f : 1.f;
        const float smooth = dsp::coefFromMs(8.f, rate);

        float pkIn = 0, pkMic = 0, pkSfx = 0, pkOut = 0;
        double sumSq = 0;

        for (uint32_t off = 0; off < n; off += kChunk) {
            const int m = int(std::min<uint32_t>(kChunk, n - off));
            for (int i = 0; i < m; ++i) {
                float x = in ? in[off + i] : 0.f;
                pkIn = std::max(pkIn, std::fabs(x));
                sumSq += double(x) * x;
                mic[i] = x * micGain;
            }
            chain.process(mic, m, cs);

            std::fill(sfxL, sfxL + m, 0.f);
            std::fill(sfxR, sfxR + m, 0.f);
            pool.render(sfxL, sfxR, m);

            for (int i = 0; i < m; ++i) {
                float sl = sfxL[i] * sfxGain, sr = sfxR[i] * sfxGain;
                float env = duckEnv.process(0.5f * (sl + sr));
                float amount = std::min(1.f, env * 30.f); // full duck above ~-30 dBFS
                float duckTarget = 1.f + (duckLin - 1.f) * amount;
                duckGain = smooth * duckGain + (1 - smooth) * duckTarget;
                muteGain = smooth * muteGain + (1 - smooth) * muteTarget;

                float v = mic[i] * muteGain * duckGain;
                pkMic = std::max(pkMic, std::fabs(v));
                pkSfx = std::max(pkSfx, std::max(std::fabs(sl), std::fabs(sr)));
                float L = v + sl, R = v + sr;
                if (limOn) limiter.process(L, R);
                else { L = std::clamp(L, -1.f, 1.f); R = std::clamp(R, -1.f, 1.f); }
                pkOut = std::max(pkOut, std::max(std::fabs(L), std::fabs(R)));
                if (vl) vl[off + i] = L;
                if (vr) vr[off + i] = R;
                if (ml) ml[off + i] = monOn ? (monMic ? L : sl) * monGain : 0.f;
                if (mr) mr[off + i] = monOn ? (monMic ? R : sr) * monGain : 0.f;
            }
        }

        // Meters: hold the max until the UI reads (exchange) them.
        peakMax(meters.micIn, pkIn);
        peakMax(meters.micOut, pkMic);
        peakMax(meters.sfx, pkSfx);
        peakMax(meters.out, pkOut);
        float msNow = n ? float(sumSq / n) : 0.f;
        noiseMs = 0.8f * noiseMs + 0.2f * msNow;
        meters.noiseFloorDb.store(10.f * std::log10(std::max(noiseMs, 1e-12f)), std::memory_order_relaxed);
        meters.gateOpen.store(chain.gateOpen(), std::memory_order_relaxed);
        meters.activeVoices.store(uint32_t(pool.active()), std::memory_order_relaxed);
        meters.cycles.fetch_add(1, std::memory_order_release);
    }

    void prepare(float r) {
        rate = r;
        chain.prepare(r);
        limiter.prepare(r);
        duckEnv.prepare(r, 5.f, 300.f);
        pool.setOutputRate(r);
        meters.sampleRate.store(uint32_t(r));
    }

    // =====================================================================
    // Graph management (PipeWire thread, loop locked)
    // =====================================================================
    uint32_t engineNode() const { return filter ? pw_filter_get_node_id(filter) : SPA_ID_INVALID; }

    uint32_t findNode(const std::string& name) const {
        for (auto& [id, n] : nodes) if (n.name == name) return id;
        return SPA_ID_INVALID;
    }

    bool isMic(const NodeInfo& n) const {
        return n.name != kVirtualMicName &&
               (n.mediaClass == "Audio/Source" || n.mediaClass == "Audio/Source/Virtual" ||
                n.mediaClass == "Audio/Duplex");
    }

    std::string resolveMic() const {
        if (!selectedMic.empty() && findNode(selectedMic) != SPA_ID_INVALID) return selectedMic;
        if (!defaultSource.empty() && defaultSource != kVirtualMicName &&
            findNode(defaultSource) != SPA_ID_INVALID)
            return defaultSource;
        for (auto& [id, n] : nodes) if (isMic(n)) return n.name; // anything that's a mic
        return {};
    }

    // Ports of `node` in the given direction, ordered FL, FR, MONO, others.
    std::vector<std::pair<uint32_t, const PortInfo*>> portsOf(uint32_t node, bool output) const {
        std::vector<std::pair<uint32_t, const PortInfo*>> v;
        for (auto& [id, p] : ports)
            if (p.node == node && p.output == output && !p.monitor) v.push_back({id, &p});
        auto rank = [](const std::string& c) { return c == "FL" ? 0 : c == "FR" ? 1 : c == "MONO" ? 2 : 3; };
        std::stable_sort(v.begin(), v.end(), [&](auto& a, auto& b) { return rank(a.second->channel) < rank(b.second->channel); });
        return v;
    }

    uint32_t ourPort(const char* portName) const {
        uint32_t en = engineNode();
        for (auto& [id, p] : ports) if (p.node == en && p.name == portName) return id;
        return SPA_ID_INVALID;
    }

    void relink() {
        std::set<std::pair<uint32_t, uint32_t>> want;
        uint32_t en = engineNode();
        linkedMic.clear();
        if (en != SPA_ID_INVALID) {
            // mic -> engine
            std::string micName = resolveMic();
            uint32_t micNode = findNode(micName);
            uint32_t inPort = ourPort("mic_in");
            if (micNode != SPA_ID_INVALID && inPort != SPA_ID_INVALID) {
                auto outs = portsOf(micNode, true);
                if (!outs.empty()) { want.insert({outs[0].first, inPort}); linkedMic = micName; }
            }
            // engine -> virtual mic (stereo, or both into mono)
            auto connectPair = [&](const char* l, const char* r, uint32_t node) {
                if (node == SPA_ID_INVALID) return;
                uint32_t pl = ourPort(l), pr = ourPort(r);
                auto ins = portsOf(node, false);
                if (ins.empty() || pl == SPA_ID_INVALID || pr == SPA_ID_INVALID) return;
                if (ins.size() == 1) { want.insert({pl, ins[0].first}); return; }
                want.insert({pl, ins[0].first});
                want.insert({pr, ins[1].first});
            };
            connectPair("vmic_FL", "vmic_FR", findNode(kVirtualMicName));
            if (monitorWanted && !defaultSink.empty())
                connectPair("mon_FL", "mon_FR", findNode(defaultSink));
        }

        for (auto it = links.begin(); it != links.end();) {
            if (!want.count(it->first)) { pw_proxy_destroy(it->second); it = links.erase(it); }
            else ++it;
        }
        for (auto& w : want) {
            if (links.count(w)) continue;
            auto po = ports.find(w.first), pi = ports.find(w.second);
            if (po == ports.end() || pi == ports.end()) continue;
            char a[16], b[16], c[16], dd[16];
            snprintf(a, sizeof a, "%u", w.first);  snprintf(b, sizeof b, "%u", w.second);
            snprintf(c, sizeof c, "%u", po->second.node); snprintf(dd, sizeof dd, "%u", pi->second.node);
            pw_properties* props = pw_properties_new(
                PW_KEY_LINK_OUTPUT_NODE, c, PW_KEY_LINK_OUTPUT_PORT, a,
                PW_KEY_LINK_INPUT_NODE, dd, PW_KEY_LINK_INPUT_PORT, b,
                PW_KEY_OBJECT_LINGER, "false", nullptr);
            auto* proxy = static_cast<pw_proxy*>(pw_core_create_object(
                core, "link-factory", PW_TYPE_INTERFACE_Link, PW_VERSION_LINK, &props->dict, 0));
            pw_properties_free(props);
            if (proxy) links[w] = proxy;
        }
        if (changed) changed();
    }

    // ---- registry ---------------------------------------------------------
    static void onGlobal(void* data, uint32_t id, uint32_t, const char* type, uint32_t,
                         const spa_dict* props) {
        auto* d = static_cast<Impl*>(data);
        if (strcmp(type, PW_TYPE_INTERFACE_Node) == 0) {
            d->nodes[id] = {prop(props, PW_KEY_NODE_NAME), prop(props, PW_KEY_NODE_DESCRIPTION),
                            prop(props, PW_KEY_MEDIA_CLASS)};
            if (d->nodes[id].description.empty()) d->nodes[id].description = d->nodes[id].name;
            d->relink();
        } else if (strcmp(type, PW_TYPE_INTERFACE_Port) == 0) {
            PortInfo p;
            p.node = uint32_t(atoi(prop(props, PW_KEY_NODE_ID)));
            p.name = prop(props, PW_KEY_PORT_NAME);
            p.channel = prop(props, PW_KEY_AUDIO_CHANNEL);
            p.output = strcmp(prop(props, PW_KEY_PORT_DIRECTION), "out") == 0;
            p.monitor = strcmp(prop(props, PW_KEY_PORT_MONITOR), "true") == 0;
            d->ports[id] = p;
            d->relink();
        } else if (strcmp(type, PW_TYPE_INTERFACE_Metadata) == 0 && !d->metadata &&
                   strcmp(prop(props, PW_KEY_METADATA_NAME), "default") == 0) {
            d->metadata = static_cast<pw_metadata*>(
                pw_registry_bind(d->registry, id, type, PW_VERSION_METADATA, 0));
            static const pw_metadata_events ev = {
                .version = PW_VERSION_METADATA_EVENTS,
                .property = &Impl::onMetadata,
            };
            pw_metadata_add_listener(d->metadata, &d->metadataListener, &ev, d);
        }
    }

    static void onGlobalRemove(void* data, uint32_t id) {
        auto* d = static_cast<Impl*>(data);
        bool touched = d->nodes.erase(id) > 0;
        if (d->ports.erase(id)) {
            touched = true;
            for (auto it = d->links.begin(); it != d->links.end();) {
                if (it->first.first == id || it->first.second == id) {
                    pw_proxy_destroy(it->second); it = d->links.erase(it);
                } else ++it;
            }
        }
        if (touched) d->relink();
    }

    static int onMetadata(void* data, uint32_t subject, const char* key, const char*, const char* value) {
        auto* d = static_cast<Impl*>(data);
        if (subject != PW_ID_CORE || !key) return 0;
        if (strcmp(key, "default.audio.source") == 0) d->defaultSource = jsonName(value);
        else if (strcmp(key, "default.audio.sink") == 0) d->defaultSink = jsonName(value);
        else return 0;
        d->relink();
        return 0;
    }

    static void onCoreError(void*, uint32_t id, int, int res, const char* message) {
        fprintf(stderr, "[voxfx] pipewire error id=%u res=%d: %s\n", id, res, message);
    }

    static void onFilterState(void* data, pw_filter_state, pw_filter_state state, const char* err) {
        auto* d = static_cast<Impl*>(data);
        if (state == PW_FILTER_STATE_ERROR) fprintf(stderr, "[voxfx] filter error: %s\n", err ? err : "?");
        d->relink();
    }

    bool hasPorts(pw_proxy* node) const {
        uint32_t id = node ? pw_proxy_get_bound_id(node) : SPA_ID_INVALID;
        if (id == SPA_ID_INVALID) return true; // not bound yet: nothing to do
        for (auto& [pid, p] : ports) if (p.node == id) return true;
        return false;
    }

    void armPortFallback() {
        pw_loop* l = pw_thread_loop_get_loop(loop);
        if (!fallbackTimer) fallbackTimer = pw_loop_add_timer(l, &Impl::onFallbackTimer, this);
        timespec value{1, 500000000}, interval{0, 0};
        pw_loop_update_timer(l, fallbackTimer, &value, &interval, false);
    }

    static void onFallbackTimer(void* data, uint64_t) {
        auto* d = static_cast<Impl*>(data);
        if (!d->hasPorts(d->vmicProxy)) {
            fprintf(stderr, "[voxfx] no session manager configured voxfx Mic; configuring ports ourselves\n");
            configureAdapterPorts(d->vmicProxy, SPA_DIRECTION_INPUT, true, 2);
        }
        if (d->toneProxy && !d->hasPorts(d->toneProxy))
            configureAdapterPorts(d->toneProxy, SPA_DIRECTION_OUTPUT, false, 1);
    }

    Port* addPort(pw_direction dir, const char* name, const char* channel) {
        return static_cast<Port*>(pw_filter_add_port(
            filter, dir, PW_FILTER_PORT_FLAG_MAP_BUFFERS, sizeof(Port),
            pw_properties_new(PW_KEY_FORMAT_DSP, "32 bit float mono audio",
                              PW_KEY_PORT_NAME, name, PW_KEY_AUDIO_CHANNEL, channel, nullptr),
            nullptr, 0));
    }
};

// =========================================================================

Engine::Engine() : d(std::make_unique<Impl>()) { d->prepare(48000.f); }
Engine::~Engine() { stop(); }

Params& Engine::params() { return d->params; }
Meters& Engine::meters() { return d->meters; }
ClipBank& Engine::bank() { return d->bank; }
bool Engine::send(const Command& c) { return d->queue.push(c); }

bool Engine::start(std::string* error) {
    pw_init(nullptr, nullptr);
    d->loop = pw_thread_loop_new("voxfx-pw", nullptr);
    d->context = pw_context_new(pw_thread_loop_get_loop(d->loop), nullptr, 0);
    if (pw_thread_loop_start(d->loop) < 0) { if (error) *error = "cannot start PipeWire thread"; return false; }

    pw_thread_loop_lock(d->loop);
    d->core = pw_context_connect(d->context, nullptr, 0);
    if (!d->core) {
        pw_thread_loop_unlock(d->loop);
        if (error) *error = "cannot connect to PipeWire (is it running?)";
        return false;
    }
    d->connected = true;
    static const pw_core_events coreEv = {
        .version = PW_VERSION_CORE_EVENTS,
        .error = &Impl::onCoreError,
    };
    pw_core_add_listener(d->core, &d->coreListener, &coreEv, d.get());

    // Virtual microphone: a null node that apps see as an input device.
    pw_properties* vp = pw_properties_new(
        "factory.name", "support.null-audio-sink",
        PW_KEY_NODE_NAME, kVirtualMicName,
        PW_KEY_NODE_DESCRIPTION, kVirtualMicDesc,
        PW_KEY_MEDIA_CLASS, "Audio/Source/Virtual",
        "audio.position", "[ FL FR ]",
        "audio.rate", "48000",
        PW_KEY_OBJECT_LINGER, "false",
        nullptr);
    d->vmicProxy = static_cast<pw_proxy*>(pw_core_create_object(
        d->core, "adapter", PW_TYPE_INTERFACE_Node, PW_VERSION_NODE, &vp->dict, 0));
    pw_properties_free(vp);
    d->armPortFallback();

    // Processing node.
    static const pw_filter_events filterEv = {
        .version = PW_VERSION_FILTER_EVENTS,
        .state_changed = &Impl::onFilterState,
        .process = &Impl::onProcess,
    };
    d->filter = pw_filter_new_simple(
        pw_thread_loop_get_loop(d->loop), "voxfx-engine",
        pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Filter",
                          PW_KEY_MEDIA_ROLE, "DSP", PW_KEY_NODE_NAME, "voxfx_engine",
                          PW_KEY_NODE_DESCRIPTION, "voxfx engine",
                          PW_KEY_NODE_LATENCY, "256/48000",
                          PW_KEY_NODE_ALWAYS_PROCESS, "true",
                          PW_KEY_NODE_DONT_RECONNECT, "true", nullptr),
        &filterEv, d.get());
    d->micIn = d->addPort(PW_DIRECTION_INPUT, "mic_in", "MONO");
    d->outVmic[0] = d->addPort(PW_DIRECTION_OUTPUT, "vmic_FL", "FL");
    d->outVmic[1] = d->addPort(PW_DIRECTION_OUTPUT, "vmic_FR", "FR");
    d->outMon[0] = d->addPort(PW_DIRECTION_OUTPUT, "mon_FL", "FL");
    d->outMon[1] = d->addPort(PW_DIRECTION_OUTPUT, "mon_FR", "FR");
    if (pw_filter_connect(d->filter, PW_FILTER_FLAG_RT_PROCESS, nullptr, 0) < 0) {
        pw_thread_loop_unlock(d->loop);
        if (error) *error = "cannot connect processing filter";
        return false;
    }

    d->registry = pw_core_get_registry(d->core, PW_VERSION_REGISTRY, 0);
    static const pw_registry_events regEv = {
        .version = PW_VERSION_REGISTRY_EVENTS,
        .global = &Impl::onGlobal,
        .global_remove = &Impl::onGlobalRemove,
    };
    pw_registry_add_listener(d->registry, &d->registryListener, &regEv, d.get());
    pw_thread_loop_unlock(d->loop);
    return true;
}

void Engine::stop() {
    if (!d->loop) return;
    pw_thread_loop_lock(d->loop);
    for (auto& [k, p] : d->links) pw_proxy_destroy(p);
    d->links.clear();
    if (d->fallbackTimer) {
        pw_loop_destroy_source(pw_thread_loop_get_loop(d->loop), d->fallbackTimer);
        d->fallbackTimer = nullptr;
    }
    if (d->metadata) { pw_proxy_destroy(reinterpret_cast<pw_proxy*>(d->metadata)); d->metadata = nullptr; }
    if (d->registry) { pw_proxy_destroy(reinterpret_cast<pw_proxy*>(d->registry)); d->registry = nullptr; }
    if (d->filter) { pw_filter_destroy(d->filter); d->filter = nullptr; }
    if (d->vmicProxy) { pw_proxy_destroy(d->vmicProxy); d->vmicProxy = nullptr; }
    if (d->toneProxy) { pw_proxy_destroy(d->toneProxy); d->toneProxy = nullptr; }
    if (d->core) { pw_core_disconnect(d->core); d->core = nullptr; }
    pw_thread_loop_unlock(d->loop);
    pw_thread_loop_stop(d->loop);
    if (d->context) { pw_context_destroy(d->context); d->context = nullptr; }
    pw_thread_loop_destroy(d->loop);
    d->loop = nullptr;
    d->connected = false;
}

std::vector<AudioDevice> Engine::microphones() {
    std::vector<AudioDevice> out;
    if (!d->loop) return out;
    pw_thread_loop_lock(d->loop);
    for (auto& [id, n] : d->nodes)
        if (d->isMic(n)) out.push_back({id, n.name, n.description});
    pw_thread_loop_unlock(d->loop);
    return out;
}

void Engine::selectMicrophone(const std::string& nodeName) {
    if (!d->loop) { d->selectedMic = nodeName; return; }
    pw_thread_loop_lock(d->loop);
    d->selectedMic = nodeName;
    d->relink();
    pw_thread_loop_unlock(d->loop);
}

std::string Engine::selectedMicrophone() {
    if (!d->loop) return d->selectedMic;
    pw_thread_loop_lock(d->loop);
    std::string s = d->selectedMic;
    pw_thread_loop_unlock(d->loop);
    return s;
}

void Engine::setMonitor(bool on) {
    d->params.monitorOn.store(on);
    if (!d->loop) { d->monitorWanted = on; return; }
    pw_thread_loop_lock(d->loop);
    d->monitorWanted = on;
    d->relink();
    pw_thread_loop_unlock(d->loop);
}

EngineStatus Engine::status() {
    EngineStatus s;
    if (!d->loop) return s;
    pw_thread_loop_lock(d->loop);
    s.connected = d->connected;
    s.virtualMicReady = d->findNode(kVirtualMicName) != SPA_ID_INVALID;
    s.engineReady = d->engineNode() != SPA_ID_INVALID;
    s.micName = d->linkedMic;
    s.monitorSink = d->monitorWanted ? d->defaultSink : std::string{};
    s.links = int(d->links.size());
    pw_thread_loop_unlock(d->loop);
    return s;
}

void Engine::setTestTone(bool on) {
    if (!d->loop) return;
    pw_thread_loop_lock(d->loop);
    if (on && !d->toneProxy) {
        pw_properties* p = pw_properties_new(
            "factory.name", "audiotestsrc",
            "library.name", "audiotestsrc/libspa-audiotestsrc",
            PW_KEY_NODE_NAME, kTestToneName,
            PW_KEY_NODE_DESCRIPTION, "voxfx Test Tone",
            PW_KEY_MEDIA_CLASS, "Audio/Source",
            "node.driver", "true",
            "node.pause-on-idle", "false",
            PW_KEY_OBJECT_LINGER, "false", nullptr);
        d->toneProxy = static_cast<pw_proxy*>(pw_core_create_object(
            d->core, "adapter", PW_TYPE_INTERFACE_Node, PW_VERSION_NODE, &p->dict, 0));
        pw_properties_free(p);
        d->armPortFallback();
    } else if (!on && d->toneProxy) {
        pw_proxy_destroy(d->toneProxy);
        d->toneProxy = nullptr;
    }
    pw_thread_loop_unlock(d->loop);
}

void Engine::setChangedCallback(std::function<void()> cb) {
    if (!d->loop) { d->changed = std::move(cb); return; }
    pw_thread_loop_lock(d->loop);
    d->changed = std::move(cb);
    pw_thread_loop_unlock(d->loop);
}

} // namespace vfx
