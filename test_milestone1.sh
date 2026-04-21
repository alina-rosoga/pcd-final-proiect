#!/usr/bin/env bash
# =============================================================================
# test_milestone1.sh - Integration test for Milestone 1 demo
# =============================================================================
# Generates a synthetic WAV file (via sox or python), runs the demo,
# and checks the output binary is well-formed.
#
# Usage:
#   ./scripts/test_milestone1.sh [AUDIO_FILE]
#   If AUDIO_FILE is not given, a 2-second synthetic sine wave is generated.
# =============================================================================

set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${PROJECT_DIR}"

GREEN='\033[0;32m'; RED='\033[0;31m'; NC='\033[0m'
pass() { echo -e "${GREEN}[PASS]${NC} $*"; }
fail() { echo -e "${RED}[FAIL]${NC} $*"; exit 1; }

AUDIO_FILE="${1:-}"

# ---- Generate test audio if none provided -----------------------------------
if [ -z "${AUDIO_FILE}" ]; then
    AUDIO_FILE="./output/test_tone.wav"
    mkdir -p output

    if command -v sox &>/dev/null; then
        sox -n -r 22050 -c 1 -b 16 "${AUDIO_FILE}" synth 2 sine 440
        echo "[test] Generated 2s 440Hz sine wave: ${AUDIO_FILE}"
    elif python3 -c "import wave,struct,math" 2>/dev/null; then
        python3 - "${AUDIO_FILE}" << 'PYEOF'
import wave, struct, math, sys
path = sys.argv[1]
sr, dur, freq = 22050, 2, 440
samples = [int(32767 * math.sin(2*math.pi*freq*t/sr)) for t in range(sr*dur)]
with wave.open(path, 'w') as f:
    f.setnchannels(1); f.setsampwidth(2); f.setframerate(sr)
    f.writeframes(struct.pack(f'<{len(samples)}h', *samples))
print(f"[test] Generated {path}")
PYEOF
    else
        fail "Need sox or python3 to generate test audio. Provide a WAV file: $0 audio.wav"
    fi
fi

[ -f "${AUDIO_FILE}" ] || fail "Audio file not found: ${AUDIO_FILE}"
[ -x "./demo_milestone1" ] || fail "demo_milestone1 not built. Run: make demo_milestone1"

# ---- Run the Milestone 1 demo -----------------------------------------------
echo "[test] Running demo_milestone1..."
OUTPUT=$(./demo_milestone1 -c config/server.cfg -f "${AUDIO_FILE}" 2>&1)
EXIT_CODE=$?

echo "${OUTPUT}"

# ---- Check exit code --------------------------------------------------------
[ "${EXIT_CODE}" -eq 0 ] || fail "demo_milestone1 exited with code ${EXIT_CODE}"
pass "Exit code: 0"

# ---- Check output file mentioned in stdout ----------------------------------
OUTPUT_FILE=$(echo "${OUTPUT}" | grep -oP '(?<=output_file : ).*' | head -1)
if [ -z "${OUTPUT_FILE}" ]; then
    # fallback: look for a .bin in output/
    OUTPUT_FILE=$(ls -t output/*.bin 2>/dev/null | head -1)
fi

[ -n "${OUTPUT_FILE}" ] || fail "No output file found in demo output"
[ -f "${OUTPUT_FILE}" ] || fail "Output file does not exist: ${OUTPUT_FILE}"
pass "Output file exists: ${OUTPUT_FILE}"

# ---- Validate binary header [n_mels(int32), n_frames(int32)] ----------------
FILE_SIZE=$(stat -c%s "${OUTPUT_FILE}")
[ "${FILE_SIZE}" -ge 8 ] || fail "Output file too small (${FILE_SIZE} bytes)"

HEADER_HEX=$(xxd -l 8 -p "${OUTPUT_FILE}")
N_MELS_HEX="${HEADER_HEX:0:8}"
N_FRAMES_HEX="${HEADER_HEX:8:8}"

# Convert little-endian hex to decimal (bash)
N_MELS=$(python3 -c "import struct; print(struct.unpack('<i', bytes.fromhex('${N_MELS_HEX}'))[0])")
N_FRAMES=$(python3 -c "import struct; print(struct.unpack('<i', bytes.fromhex('${N_FRAMES_HEX}'))[0])")

[ "${N_MELS}" -gt 0 ]   || fail "n_mels is 0 in output header"
[ "${N_FRAMES}" -gt 0 ] || fail "n_frames is 0 in output header"

EXPECTED_SIZE=$(( 8 + N_MELS * N_FRAMES * 4 ))
[ "${FILE_SIZE}" -eq "${EXPECTED_SIZE}" ] || \
    fail "Output size mismatch: got ${FILE_SIZE}, expected ${EXPECTED_SIZE} (${N_MELS}x${N_FRAMES} floats)"

pass "Output binary valid: ${N_MELS} Mel bands x ${N_FRAMES} frames"
pass "File size: ${FILE_SIZE} bytes (header 8 + ${N_MELS}*${N_FRAMES}*4)"

# ---- Check stdout contains required M1 items --------------------------------
echo "${OUTPUT}" | grep -q "libconfig\|Config loaded\|port="  || \
    warn "libconfig output not detected (might use defaults)"
echo "${OUTPUT}" | grep -q "HOME\|USER\|PATH"  || \
    warn "getenv output not detected"
echo "${OUTPUT}" | grep -q "Forking\|fork\|child"  || \
    warn "fork() output not detected"

echo ""
pass "=== Milestone 1 integration test PASSED ==="
