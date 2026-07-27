"""Builds the CIA banner audio from Armagetron's own title track.

The 3DS HOME menu wants a three second, sixteen bit stereo CWAV at 16364 Hz.
bannertool will convert a WAV, but it converts whatever it is given: hand it a
mono file at twice the rate and the menu reads the result as noise. Both known
good homebrew banners to compare against ship exactly 49092 stereo frames at
16364 Hz, so that is what this writes.

The result is committed as platform/3ds/banner.wav, so building a CIA does not
need this script; it is here so the banner can be made again from a different
excerpt or a different track. Needs ffmpeg on the path and numpy.

    python platform/3ds/tools/make-banner-audio.py
"""

import os
import struct
import subprocess
import sys

import numpy as np

RATE = 16364
FRAMES = 49092          # exactly three seconds, the menu's limit

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.normpath(os.path.join(HERE, '..', '..', '..'))
SOURCE = os.path.join(PROJECT, 'music', 'titletrack.ogg')
OUTPUT = os.path.normpath(os.path.join(HERE, '..', 'banner.wav'))
SCRATCH = os.path.join(HERE, 'title-resampled.wav')

subprocess.run(
    ['ffmpeg', '-v', 'error', '-y', '-i', SOURCE,
     '-ac', '2', '-ar', str(RATE), '-acodec', 'pcm_s16le', SCRATCH],
    check=True)


def read_wav(path):
    d = open(path, 'rb').read()
    pos = 12
    fmt = None
    data = None
    while pos + 8 <= len(d):
        cid = d[pos:pos + 4]
        csz = struct.unpack_from('<I', d, pos + 4)[0]
        if cid == b'fmt ':
            fmt = struct.unpack_from('<HHIIHH', d, pos + 8)
        elif cid == b'data':
            data = np.frombuffer(d, dtype='<i2', count=csz // 2, offset=pos + 8)
        pos += 8 + csz + (csz & 1)
    return fmt, data


fmt, raw = read_wav(SCRATCH)
channels = fmt[1]
audio = raw.reshape(-1, channels).astype(np.float64) / 32768.0
print('decoded %d frames at %d Hz, %d channels' % (len(audio), fmt[2], channels))

if len(audio) < FRAMES:
    sys.exit('title track is shorter than three seconds')

# Pick the window. Loudness alone would land in the middle of a phrase, so
# prefer a window that starts quietly and grows: that reads as a beginning
# rather than as something cut out of the middle.
mono = audio.mean(axis=1)
energy = mono ** 2
window = RATE // 20                       # 50 ms
smooth = np.convolve(energy, np.ones(window) / window, mode='same')

best = None
for start in range(0, len(audio) - FRAMES + 1, RATE // 20):
    segment = smooth[start:start + FRAMES]
    onset = segment[:RATE // 4].mean()     # first quarter second
    body = segment[RATE // 4:].mean()
    if body <= 0:
        continue
    # Loud overall, and quieter at the start than through the body.
    score = body * (1.0 - min(onset / body, 1.0) * 0.5)
    if best is None or score > best[0]:
        best = (score, start)

start = best[1]
print('chose %.2f s .. %.2f s of %.2f s'
      % (start / float(RATE), (start + FRAMES) / float(RATE), len(audio) / float(RATE)))

clip = audio[start:start + FRAMES].copy()

# The menu loops this, so both ends have to reach silence or the seam clicks.
fade_in = int(RATE * 0.05)
fade_out = int(RATE * 0.35)
clip[:fade_in] *= np.linspace(0.0, 1.0, fade_in)[:, None] ** 2
clip[-fade_out:] *= np.linspace(1.0, 0.0, fade_out)[:, None] ** 2

# Match the loudness of the two working banners rather than running it up to
# full scale; the menu mixes this under its own sounds.
peak = np.abs(clip).max()
if peak > 0:
    clip *= 0.55 / peak

samples = np.clip(np.round(clip * 32767.0), -32768, 32767).astype('<i2')
payload = samples.tobytes()

with open(OUTPUT, 'wb') as out:
    out.write(b'RIFF')
    out.write(struct.pack('<I', 36 + len(payload)))
    out.write(b'WAVEfmt ')
    out.write(struct.pack('<IHHIIHH', 16, 1, 2, RATE, RATE * 2 * 2, 4, 16))
    out.write(b'data')
    out.write(struct.pack('<I', len(payload)))
    out.write(payload)

os.remove(SCRATCH)
print('wrote %s: %d bytes, %d frames, peak %.3f, rms %.4f'
      % (OUTPUT, 44 + len(payload), FRAMES,
         float(np.abs(clip).max()), float(np.sqrt((clip ** 2).mean()))))
