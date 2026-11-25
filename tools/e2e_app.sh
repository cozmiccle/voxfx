#!/usr/bin/env bash
# Whole-app check, headless: starts the real voxfx GUI (offscreen) in a throwaway
# PipeWire + WirePlumber session with a seeded library and checks, through
# `voxfx ctl`, what an app selecting "voxfx Mic" hears:
#   1. a soundboard sound triggered with `voxfx ctl play`
#   2. a new sound recorded from the (fake) mic with record-start/record-stop,
#      played back after the live voice has stopped
# Saves screenshots to $SHOT_DIR (default: current directory).
#
# Usage: tools/e2e_app.sh [build-dir]
set -euo pipefail
BUILD=$(realpath "${1:-build}")
SHOT_DIR=$(realpath -m "${SHOT_DIR:-.}")
mkdir -p "$SHOT_DIR"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
export HOME="$WORK/home" XDG_DATA_HOME="$WORK/home/.local/share" XDG_CONFIG_HOME="$WORK/home/.config"
export XDG_RUNTIME_DIR="$WORK/run"
mkdir -p "$XDG_DATA_HOME/voxfx/sounds" "$XDG_DATA_HOME/voxfx/images" "$XDG_CONFIG_HOME" && mkdir -m 700 "$XDG_RUNTIME_DIR"

mkdir -p "$XDG_CONFIG_HOME/voxfx"
printf '[ui]\ntheme=%s\npadLayout=%s\npadSize=%s\n' "${THEME:-dark}" "${LAYOUT:-auto}" "${PAD_SIZE:-170}" \
    > "$XDG_CONFIG_HOME/voxfx/voxfx.conf"

python3 - "$XDG_DATA_HOME/voxfx" "$WORK" <<'EOF'
import json, math, struct, sys, wave
d, work = sys.argv[1], sys.argv[2]
def tone(path, f, secs, amp=9000, rate=44100):
    w = wave.open(path, "wb"); w.setnchannels(1); w.setsampwidth(2); w.setframerate(rate)
    w.writeframes(b"".join(struct.pack("<h", int(amp * math.sin(2 * math.pi * f * i / rate)))
                           for i in range(int(rate * secs))))
def bmp(path, w, h, fn):  # tiny 24-bit BMP writer for a test pad image
    row = (w * 3 + 3) & ~3
    data = b"".join(b"".join(bytes(fn(x, y)) for x in range(w)) + b"\0" * (row - w * 3) for y in range(h))
    hdr = b"BM" + struct.pack("<IHHI", 54 + len(data), 0, 0, 54)
    hdr += struct.pack("<IiiHHIIiiII", 40, w, h, 1, 24, 0, len(data), 2835, 2835, 0, 0)
    open(path, "wb").write(hdr + data)
tone(f"{d}/sounds/airhorn.wav", 1320, 1.5)
tone(f"{d}/sounds/boing.wav", 330, 0.8)
tone(f"{d}/sounds/drumroll.wav", 90, 6.0)
tone(f"{work}/voice.wav", 220, 12, 6000, 48000)  # fake voice for the mic
bmp(f"{d}/images/horn.bmp", 96, 96, lambda x, y: (40 + y, 90 + x, 200))  # BGR gradient
sounds = [
    {"id": "a1", "title": "Air horn", "file": "airhorn.wav", "image": "horn.bmp", "mode": "restart"},
    {"id": "b2", "title": "Boing", "file": "boing.wav", "source": "myinstants",
     "sourceUrl": "https://www.myinstants.com/en/instant/example/", "mode": "overlap"},
    {"id": "c3", "title": "Drum roll", "file": "drumroll.wav", "mode": "hold"},
    {"id": "d4", "title": "Sad trombone", "file": "boing.wav", "mode": "restart"},
    {"id": "e5", "title": "Applause", "file": "airhorn.wav", "mode": "loop"},
]
bindings = [
    {"device": "evdev:1234:5678:Macro Pad", "deviceLabel": "Macro Pad", "control": "key:183",
     "controlLabel": "KEY_F13", "mods": 0, "bank": -1, "action": "play", "sound": "a1"},
    {"device": "midi:MPD218", "deviceLabel": "MPD218", "control": "note:9:36",
     "controlLabel": "Note 36 (ch 10)", "mods": 0, "bank": -1, "action": "play", "sound": "b2"},
]
json.dump({"version": 1, "sounds": sounds, "bindings": bindings}, open(f"{d}/library.json", "w"))
EOF

dbus-run-session -- bash -s "$BUILD" "$WORK" "$SHOT_DIR" <<'EOS'
set -euo pipefail
BUILD=$1; WORK=$2; SHOT_DIR=$3
ctl() { "$BUILD/voxfx" ctl "$@"; }
now() { date +%s.%N; }
pipewire >"$WORK/pw.log" 2>&1 &
sleep 0.8
wireplumber >"$WORK/wp.log" 2>&1 &
sleep 1.2
QT_QPA_PLATFORM=offscreen VOXFX_DEV=1 VOXFX_DEV_QUIT_MS=30000 "$BUILD/voxfx" >"$WORK/app.log" 2>&1 &
APP=$!
sleep 2.5
pw-record --target voxfx_mic --channels 2 --rate 48000 "$WORK/heard.wav" >/dev/null 2>&1 &
REC=$!
T_REC=$(now)
pw-cat --playback "$WORK/voice.wav" -P '{ node.name=fake_voice node.autoconnect=false }' >/dev/null 2>&1 &
VOICE=$!
sleep 0.7
pw-link "$(pw-link -o | grep '^fake_voice:' | head -1)" voxfx_engine:mic_in

echo "--- ctl list:"; ctl list
echo "--- record a take from the mic:"
ctl record-start
sleep 2
ctl record-stop "My take"
kill $VOICE; sleep 0.6            # live voice gone from here on
T_HORN=$(now); ctl play "air horn"
sleep 0.4
ctl play "drum roll"
sleep 0.3
ctl dev-screenshot "$SHOT_DIR/voxfx-playing.png"
sleep 1.2
T_TAKE=$(now); ctl play "My take"
sleep 2.6
kill -INT $REC; sleep 0.5
ctl list | tail -1
ctl dev-screenshot "$SHOT_DIR/voxfx-screenshot.png"
ctl dev-screenshot "record:$SHOT_DIR/voxfx-record-dialog.png"
sleep 0.8
ctl dev-screenshot "edit:a1:$SHOT_DIR/voxfx-edit-dialog.png"
sleep 0.8
ctl dev-screenshot "size:780x520:$SHOT_DIR/voxfx-small.png"
sleep 0.6
kill $APP; wait $APP 2>/dev/null || true
echo "$T_REC $T_HORN $T_TAKE" > "$WORK/times"
echo "--- app log:"; grep -v propagateSizeHints "$WORK/app.log" || true
EOS

python3 - "$WORK/heard.wav" $(cat "$WORK/times") <<'EOF'
import math, struct, sys, wave
w = wave.open(sys.argv[1]); rate = w.getframerate(); n = w.getnframes()
t_rec, t_horn, t_take = map(float, sys.argv[2:5])
left = struct.unpack("<%dh" % (n * w.getnchannels()), w.readframes(n))[0::w.getnchannels()]
def level(freq, start, secs):
    seg = left[max(0, int(start * rate)):int((start + secs) * rate)]
    if not seg: return -120.0
    k = 2 * math.cos(2 * math.pi * freq / rate); s1 = s2 = 0.0
    for x in seg: s0 = x / 32768 + k * s1 - s2; s2, s1 = s1, s0
    return 10 * math.log10(max((s1*s1 + s2*s2 - k*s1*s2) / len(seg) ** 2, 1e-12)) + 6
def best(freq, start, secs, step=0.1, win=0.3):
    return max(level(freq, start + i * step, win) for i in range(int((secs - win) / step) + 1))
horn = best(1320, t_horn - t_rec, 1.2)
gap = best(220, t_horn - t_rec + 0.1, (t_take - t_horn) - 0.2)   # voice stopped, take not yet played
take = best(220, t_take - t_rec, 1.8)
print(f"--- heard on voxfx Mic: air horn {horn:.1f} dBFS | 220 Hz before take {gap:.1f} dBFS | recorded take {take:.1f} dBFS")
ok = horn > -40 and take > -40 and gap < take - 20
print("APP E2E PASS" if ok else "APP E2E FAIL")
sys.exit(0 if ok else 1)
EOF
echo "screenshots: $SHOT_DIR/voxfx-screenshot.png $SHOT_DIR/voxfx-playing.png"
