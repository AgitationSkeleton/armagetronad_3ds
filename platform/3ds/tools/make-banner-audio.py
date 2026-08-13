"""Builds a candidate CIA banner tune from Armagetron's own title track.

WARNING: what this produces has been tried on hardware and came out wrong on
the HOME menu, despite measuring correct on every count that can be measured
here: sixteen bit stereo, 16364 Hz, 49092 frames, the same shape as banners
that do work. Whatever the console objects to is not something these checks
catch, so treat the output as a starting point to be tested, not as a banner.

The banner actually shipped, platform/3ds/banner.wav, is the one from
JavaTron3DS, which is known to play correctly. This script will not overwrite
it. It writes beside it under a different name, and installing the result is a
deliberate copy you make yourself once you have heard it on a console.

Needs ffmpeg on the path and numpy.

    python platform/3ds/tools/make-banner-audio.py [output.wav]
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
SCRATCH = os.path.join(HERE, 'title-resampled.wav')

SHIPPED = os.path.normpath(os.path.join(HERE, '..', 'banner.wav'))
OUTPUT = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else \
    os.path.normpath(os.path.join(HERE, '..', 'banner-candidate.wav'))

# The banner that ships works. This one has not been shown to, so it does not
# get to replace it by default, and it does not get to replace it by accident
# through an argument either.
if os.path.normcase(OUTPUT) == os.path.normcase(SHIPPED):
    sys.exit('refusing to overwrite the shipped banner.\n'
             'Write somewhere else, listen to it on a console, and copy it '
             'over yourself if it is good.')

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
