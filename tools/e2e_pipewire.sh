#!/usr/bin/env bash
# End-to-end check in a throwaway PipeWire + WirePlumber session (no sound card
# needed). Verifies what Discord would hear from "voxfx Mic":
#   1. a 220 Hz "voice" fed into the engine's mic input comes out of voxfx Mic
#   2. a soundboard clip played by the engine comes out of voxfx Mic
#   3. an ordinary app recording with --target voxfx_mic gets linked by WirePlumber
#
# Usage: tools/e2e_pipewire.sh [build-dir]     (needs pipewire, wireplumber,
#        dbus-run-session, pw-cat/pw-record/pw-link, python3)
set -euo pipefail
BUILD=${1:-build}
SMOKE=$(realpath "$BUILD/voxfx-engine-smoke")
WORK=$(mktemp -d)
[ -n "${KEEP:-}" ] && echo "work dir: $WORK" || trap 'rm -rf "$WORK"' EXIT

python3 - "$WORK" <<'EOF'
import math, struct, sys, wave
d = sys.argv[1]
def tone(path, f, secs, amp, rate):
    w = wave.open(path, "wb"); w.setnchannels(1); w.setsampwidth(2); w.setframerate(rate)
    w.writeframes(b"".join(struct.pack("<h", int(amp * math.sin(2 * math.pi * f * i / rate)))
                           for i in range(int(rate * secs))))
tone(f"{d}/voice.wav", 220, 8, 6000, 48000)   # fake voice into the mic input
tone(f"{d}/clip.wav", 1320, 1.5, 9000, 44100)  # soundboard clip (needs resampling)
EOF

export XDG_RUNTIME_DIR="$WORK/run"
mkdir -m 700 -p "$XDG_RUNTIME_DIR"

dbus-run-session -- bash -s "$SMOKE" "$WORK" <<'EOS'
set -euo pipefail
SMOKE=$1; WORK=$2
pipewire >"$WORK/pw.log" 2>&1 &
sleep 0.8
wireplumber >"$WORK/wp.log" 2>&1 &
sleep 1.2

# Engine runs ~7 s and plays the clip ~3 s in.
VOXFX_SMOKE_PLAY_AT=3000 timeout 12 "$SMOKE" 7 "$WORK/clip.wav" >"$WORK/smoke.txt" 2>&1 &
sleep 1.0
pw-cat --playback "$WORK/voice.wav" -P '{ node.name=fake_voice node.autoconnect=false }' >/dev/null 2>&1 &
# A normal app picking "voxfx Mic" as its microphone; WirePlumber does the linking.
pw-record --target voxfx_mic --channels 2 --rate 48000 "$WORK/heard.wav" >/dev/null 2>&1 &
REC=$!
sleep 0.8
VOICE_OUT=$(pw-link -o | grep '^fake_voice:' | head -1)
pw-link "$VOICE_OUT" voxfx_engine:mic_in
echo "--- links"; pw-link -l | grep -E 'voxfx|rec|fake' || true
sleep 5
kill -INT $REC 2>/dev/null || true
sleep 1.5
echo "--- engine"; cat "$WORK/smoke.txt"
EOS

python3 - "$WORK/heard.wav" <<'EOF'
import math, struct, sys, wave
w = wave.open(sys.argv[1]); rate = w.getframerate(); n = w.getnframes()
data = struct.unpack("<%dh" % (n * w.getnchannels()), w.readframes(n))
left = data[0::w.getnchannels()]
def level(freq, start, secs):
    seg = left[int(start * rate):int((start + secs) * rate)]
    if not seg: return -120.0
    # Goertzel: energy at one frequency
    k = 2 * math.cos(2 * math.pi * freq / rate); s1 = s2 = 0.0
    for x in seg: s0 = x / 32768 + k * s1 - s2; s2, s1 = s1, s0
    p = s1 * s1 + s2 * s2 - k * s1 * s2
    return 10 * math.log10(max(p / len(seg) ** 2, 1e-12)) + 6
dur = n / rate
voice = max(level(220, t, 0.5) for t in [x * 0.5 for x in range(int(dur * 2))])
clip = max(level(1320, t, 0.5) for t in [x * 0.5 for x in range(int(dur * 2))])
print(f"--- recorded {dur:.1f}s from voxfx Mic: voice(220Hz) {voice:.1f} dBFS, clip(1320Hz) {clip:.1f} dBFS")
ok = voice > -40 and clip > -40
print("E2E PASS" if ok else "E2E FAIL")
sys.exit(0 if ok else 1)
EOF
