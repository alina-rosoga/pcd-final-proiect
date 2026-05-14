#!/bin/bash
if [ -z "$1" ]; then
    echo "Utilizare: bash run.sh <fisier.mp3/mp4/wav>"
    exit 1
fi

INPUT="$1"
WAV="output/input_converted.wav"

echo " Conversie audio "
ffmpeg -i "$INPUT" -ac 1 -ar 22050 -sample_fmt s16 "$WAV" -y 2>/dev/null
echo "Convertit: $WAV"

echo "Compilare"
g++ -std=c++14 -D_POSIX_C_SOURCE=200809L \
    -Iinclude -Isrc -I$HOME/.local/include -I/usr/include/eigen3 \
    -o demo_milestone1 \
    src/demo_milestone1.cpp \
    -x c src/config_loader.c -x none \
    -lconfig -lm 2>&1 | grep error || true

echo "Rulare demo"
./demo_milestone1 -c config/server.cfg -f "$WAV"

echo "Generare spectrograma"
python3 << 'PYEOF'
import numpy as np
import matplotlib.pyplot as plt
import struct, glob, os

files = sorted(glob.glob('./output/*.bin'), key=os.path.getmtime)
path = files[-1]
print(f"Citesc: {path}")

with open(path, 'rb') as f:
    n_mels   = struct.unpack('<i', f.read(4))[0]
    n_frames = struct.unpack('<i', f.read(4))[0]
    data = np.frombuffer(f.read(), dtype=np.float32)

mel_db = data.reshape(n_mels, n_frames)

plt.figure(figsize=(16, 6))
plt.imshow(mel_db, origin='lower', aspect='auto', cmap='inferno')
plt.colorbar(label='dB')
plt.title('Mel Spectrogram')
plt.xlabel('Frame (timp)')
plt.ylabel('Mel Band (frecventa)')
plt.tight_layout()
plt.savefig('./output/spectrogram.png', dpi=150)
print("Salvat: ./output/spectrogram.png")
PYEOF

echo ""
echo " GATA! Deschide output/spectrogram.png "
