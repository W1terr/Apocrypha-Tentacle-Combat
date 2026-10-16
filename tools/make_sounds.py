"""Makes the tentacle death sounds (res/Sound/FX/TentacleCombat/death_0N.wav): layered from vanilla sounds - a wet
blade cut, a gore splat, Apocrypha slime, a pitched down Seeker death cry (Hermaeus Mora's creatures) and the heavy
drop of Miraak's death tentacles - then compressed and normalized. Mono, 16 bit, 44.1 kHz.

usage: python make_sounds.py   (needs numpy + scipy)
"""
from pathlib import Path

import numpy as np
from scipy import signal
from scipy.io import wavfile

import gamefiles

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "res" / "Sound" / "FX" / "TentacleCombat"
RATE = 44100

GORE = r"sound\fx\npc\kill\gore\npc_kill_gore_0{}.wav"
CUT = r"sound\fx\wpn\impact\blade\2hand\flesh\wpn_impact_blade2hand_flesh_0{}.wav"
AXE = r"sound\fx\wpn\impact\axelarge\flesh\wpn_impact_axelarge_flesh_0{}.wav"
SLIME = r"sound\fx\dlc02\ambr\telmithryn\ambr_telmithryn_slime_0{}.wav"
CRY = r"sound\fx\dlc02\npc\seeker\npc_seeker_death_3d_0{}.wav"
DROP = r"sound\fx\dlc02\qst\miraakdeath\qst_miraakdeath_tentacle_drop_01.wav"
HIT = r"sound\fx\dlc02\trp\trp_apocryphatentacle_hit_0{}.wav"

# (file, start seconds, gain, pitch factor, lowpass Hz or 0)
VARIANTS = [
    [(CUT.format(1), 0.00, 0.8, 1.0, 0), (GORE.format(1), 0.01, 0.9, 1.0, 0), (SLIME.format(1), 0.05, 0.6, 1.0, 0),
     (CRY.format(1), 0.10, 0.8, 0.82, 0), (GORE.format(1), 0.0, 0.5, 0.55, 300), (DROP, 0.30, 0.45, 1.0, 0)],
    [(CUT.format(2), 0.00, 0.8, 1.0, 0), (GORE.format(2), 0.01, 0.9, 1.0, 0), (SLIME.format(3), 0.04, 0.6, 1.0, 0),
     (CRY.format(2), 0.09, 0.8, 0.78, 0), (GORE.format(2), 0.0, 0.5, 0.5, 300), (HIT.format(2), 0.28, 0.4, 0.8, 0)],
    [(AXE.format(1), 0.00, 0.8, 1.0, 0), (GORE.format(3), 0.01, 0.9, 1.0, 0), (SLIME.format(5), 0.05, 0.6, 1.0, 0),
     (CRY.format(1), 0.11, 0.75, 0.7, 0), (GORE.format(3), 0.0, 0.5, 0.5, 300), (DROP, 0.32, 0.45, 0.9, 0)],
]


def load(name):
    rate, data = wavfile.read(gamefiles.get(name))
    data = data.astype(np.float64)
    if data.ndim > 1:
        data = data.mean(axis=1)
    data /= 32768.0
    if rate != RATE:
        data = signal.resample_poly(data, RATE, rate)
    return data


# longest part of each source that is kept (seconds, faded out over the last third)
MAX_LENGTH = {SLIME: 1.2, CRY: 2.2, DROP: 1.8, HIT: 1.4}


def layer(name, gain, pitch, lowpass):
    data = load(name)
    limit = next((v for k, v in MAX_LENGTH.items() if name.startswith(k.split("{")[0])), None)
    if limit and len(data) > limit * RATE:
        data = data[:int(limit * RATE)].copy()
        fade = len(data) // 3
        data[-fade:] *= np.linspace(1.0, 0.0, fade) ** 2
    if pitch != 1.0:
        # slower playback = lower pitch: resample to more samples
        up, down = int(round(1000 / pitch)), 1000
        data = signal.resample_poly(data, up, down)
    if lowpass:
        b, a = signal.butter(4, lowpass / (RATE / 2), "low")
        data = signal.lfilter(b, a, data) * 2.0
    peak = np.max(np.abs(data)) or 1.0
    return data / peak * gain


def make(variant):
    length = int(RATE * 3.0)
    mix = np.zeros(length)
    for name, start, gain, pitch, lowpass in variant:
        data = layer(name, gain, pitch, lowpass)
        s = int(start * RATE)
        n = min(len(data), length - s)
        mix[s:s + n] += data[:n]
    # trim the silent tail, fade the end
    level = np.abs(mix)
    last = np.nonzero(level > 0.003)[0]
    mix = mix[:last[-1] + 1] if len(last) else mix
    fade = min(len(mix), int(0.15 * RATE))
    mix[-fade:] *= np.linspace(1.0, 0.0, fade)
    # gentle compression, then -1 dBFS peak
    mix = np.tanh(mix * 1.6) / np.tanh(1.6)
    mix *= 10 ** (-1 / 20) / np.max(np.abs(mix))
    return (mix * 32767).astype(np.int16)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for i, variant in enumerate(VARIANTS, 1):
        data = make(variant)
        path = OUT / f"death_{i:02d}.wav"
        wavfile.write(path, RATE, data)
        rms = np.sqrt(np.mean((data / 32768.0) ** 2))
        print(f"wrote {path.name}: {len(data) / RATE:.2f} s, RMS {20 * np.log10(rms):.1f} dBFS")


if __name__ == "__main__":
    main()
