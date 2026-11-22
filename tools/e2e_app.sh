#!/usr/bin/env bash
# Whole-app check, headless: starts the real voxfx GUI (offscreen) in a throwaway
# PipeWire + WirePlumber session with a seeded library, triggers a sound through
# `voxfx ctl`, and records what an app selecting "voxfx Mic" hears.
# Also saves a screenshot of the window to $SHOT (default: ./voxfx-screenshot.png).
#
# Usage: tools/e2e_app.sh [build-dir]
set -euo pipefail
BUILD=$(realpath "${1:-build}")
SHOT=$(realpath -m "${SHOT:-voxfx-screenshot.png}")
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
export HOME="$WORK/home" XDG_DATA_HOME="$WORK/home/.local/share" XDG_CONFIG_HOME="$WORK/home/.config"
export XDG_RUNTIME_DIR="$WORK/run"
mkdir -p "$XDG_DATA_HOME/voxfx/sounds" "$XDG_CONFIG_HOME" && mkdir -m 700 "$XDG_RUNTIME_DIR"

python3 - "$XDG_DATA_HOME/voxfx" <<'EOF'
import json, math, struct, sys, wave
d = sys.argv[1]
def tone(name, f, secs, rate=44100):
    w = wave.open(f"{d}/sounds/{name}", "wb"); w.setnchannels(1); w.setsampwidth(2); w.setframerate(rate)
    w.writeframes(b"".join(struct.pack("<h", int(9000 * math.sin(2 * math.pi * f * i / rate)))
                           for i in range(int(rate * secs))))
tone("airhorn.wav", 1320, 1.5); tone("boing.wav", 330, 0.8); tone("drumroll.wav", 90, 2.0)
sounds = [
    {"id": "a1", "title": "Air horn", "file": "airhorn.wav", "source": "local", "mode": "restart"},
    {"id": "b2", "title": "Boing", "file": "boing.wav", "source": "myinstants",
     "sourceUrl": "https://www.myinstants.com/en/instant/example/", "mode": "overlap"},
    {"id": "c3", "title": "Drum roll", "file": "drumroll.wav", "source": "local", "mode": "hold"},
]
bindings = [
    {"device": "evdev:1234:5678:Macro Pad", "deviceLabel": "Macro Pad", "control": "key:183",
     "controlLabel": "KEY_F13", "mods": 0, "bank": -1, "action": "play", "sound": "a1"},
    {"device": "midi:MPD218", "deviceLabel": "MPD218", "control": "note:9:36",
     "controlLabel": "Note 36 (ch 10)", "mods": 0, "bank": -1, "action": "play", "sound": "b2"},
]
json.dump({"version": 1, "sounds": sounds, "bindings": bindings}, open(f"{d}/library.json", "w"))
EOF

dbus-run-session -- bash -s "$BUILD" "$WORK" "$SHOT" <<'EOS'
set -euo pipefail
BUILD=$1; WORK=$2; SHOT=$3
pipewire >"$WORK/pw.log" 2>&1 &
sleep 0.8
wireplumber >"$WORK/wp.log" 2>&1 &
sleep 1.2
QT_QPA_PLATFORM=offscreen VOXFX_DEV_SCREENSHOT="$SHOT" VOXFX_DEV_QUIT_MS=9000 \
    "$BUILD/voxfx" >"$WORK/app.log" 2>&1 &
APP=$!
sleep 2.5
pw-record --target voxfx_mic --channels 2 --rate 48000 "$WORK/heard.wav" >/dev/null 2>&1 &
REC=$!
sleep 1
echo "--- ctl list:"; "$BUILD/voxfx" ctl list
echo "--- ctl play:"; "$BUILD/voxfx" ctl play "air horn"
"$BUILD/voxfx" ctl play nope || true
sleep 2.5
kill -INT $REC; sleep 0.5
wait $APP || true
echo "--- app log:"; cat "$WORK/app.log"
EOS

python3 - "$WORK/heard.wav" <<'EOF'
import math, struct, sys, wave
w = wave.open(sys.argv[1]); rate = w.getframerate(); n = w.getnframes()
left = struct.unpack("<%dh" % (n * w.getnchannels()), w.readframes(n))[0::w.getnchannels()]
def level(freq, start, secs):
    seg = left[int(start * rate):int((start + secs) * rate)]
    if not seg: return -120.0
    k = 2 * math.cos(2 * math.pi * freq / rate); s1 = s2 = 0.0
    for x in seg: s0 = x / 32768 + k * s1 - s2; s2, s1 = s1, s0
    return 10 * math.log10(max((s1*s1 + s2*s2 - k*s1*s2) / len(seg) ** 2, 1e-12)) + 6
dur = n / rate
horn = max([level(1320, t * 0.25, 0.5) for t in range(int(dur * 4))] or [-120])
print(f"--- recorded {dur:.1f}s from voxfx Mic: air horn (1320Hz) {horn:.1f} dBFS")
print("APP E2E PASS" if horn > -40 else "APP E2E FAIL")
sys.exit(0 if horn > -40 else 1)
EOF
echo "screenshot: $SHOT"
