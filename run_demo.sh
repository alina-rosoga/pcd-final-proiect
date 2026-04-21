#!/bin/bash

# 1. Ruleaza demo pe riana.wav
./demo_milestone1 -c config/server.cfg -f output/riana.wav

# 2. Genereaza poza spectrogramei
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

plt.figure(figsize=(14, 6))
plt.imshow(mel_db, origin='lower', aspect='auto', cmap='inferno')
plt.colorbar(label='dB')
plt.title('Mel Spectrogram - Riana')
plt.xlabel('Frame (timp)')
plt.ylabel('Mel Band (frecventa)')
plt.tight_layout()
plt.savefig('./output/spectrogram_riana.png', dpi=150)
print("Salvat: ./output/spectrogram_riana.png")
PYEOF
