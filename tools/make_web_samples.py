"""
make_web_samples.py - synthesizes the dry demo sounds used on the website
playground (no recordings needed, so there are no licensing questions).

    python tools/make_web_samples.py website/eighth-hall/samples

Writes 48 kHz stereo WAVs; convert to MP3 for the web, e.g.
    ffmpeg -i drums.wav -b:a 192k drums.mp3

Requires: numpy, scipy
"""
import os
import sys

import numpy as np
from scipy.io import wavfile
from scipy.signal import butter, sosfilt

SR = 48000
rng = np.random.default_rng(7)


def silence(seconds):
    return np.zeros(int(SR * seconds))


def place(track, sound, start_sec, gain=1.0, wrap=False):
    """Mix `sound` into `track`. Every sound gets a 30 ms fade at its end so
    notes never stop with a click. With wrap=True, anything running past the
    end continues at the start (for seamless loops)."""
    sound = sound.copy()
    fade = min(len(sound), int(0.03 * SR))
    sound[-fade:] *= np.linspace(1, 0, fade)
    i = int(start_sec * SR)
    n = min(len(sound), len(track) - i)
    if n > 0:
        track[i:i + n] += gain * sound[:n]
    if wrap and len(sound) > n:
        rest = sound[n:]
        track[:len(rest)] += gain * rest


def env(n, decay_sec, attack_sec=0.001):
    t = np.arange(n) / SR
    a = np.clip(t / attack_sec, 0, 1)
    return a * np.exp(-t / decay_sec)


def bandpass(x, lo, hi, order=2):
    return sosfilt(butter(order, [lo, hi], btype="band", fs=SR, output="sos"), x)


def highpass(x, f, order=2):
    return sosfilt(butter(order, f, btype="high", fs=SR, output="sos"), x)


def lowpass(x, f, order=2):
    return sosfilt(butter(order, f, btype="low", fs=SR, output="sos"), x)


# ---- drum voices -------------------------------------------------------------
def kick():
    n = int(0.5 * SR)
    t = np.arange(n) / SR
    freq = 45 + 85 * np.exp(-t / 0.035)
    phase = 2 * np.pi * np.cumsum(freq) / SR
    body = np.sin(phase) * env(n, 0.18)
    click = highpass(rng.standard_normal(n), 2000) * env(n, 0.004)
    return 0.9 * body + 0.15 * click


def snare():
    n = int(0.4 * SR)
    t = np.arange(n) / SR
    tone = (np.sin(2 * np.pi * 185 * t) + 0.5 * np.sin(2 * np.pi * 330 * t)) * env(n, 0.045)
    noise = bandpass(rng.standard_normal(n), 1500, 9000) * env(n, 0.09)
    return 0.45 * tone + 0.55 * noise


def hat(open_=False):
    n = int((0.35 if open_ else 0.08) * SR)
    noise = highpass(rng.standard_normal(n), 7000, order=4)
    return 0.3 * noise * env(n, 0.12 if open_ else 0.018)


def drums():
    bpm = 96
    beat = 60 / bpm
    bars = 2
    track = silence(bars * 4 * beat)   # exactly 2 bars: loops in time
    for bar in range(bars):
        b0 = bar * 4 * beat
        for k in [0, 1.5, 2.5] if bar == 0 else [0, 1.5, 2.75]:
            place(track, kick(), b0 + k * beat, 0.9, wrap=True)
        for s in [1, 3]:
            place(track, snare(), b0 + s * beat, 0.8, wrap=True)
        for h in range(8):
            place(track, hat(open_=(h == 7 and bar == 1)), b0 + h * beat / 2, 0.7 if h % 2 == 0 else 0.45, wrap=True)
    return track, track.copy()


def snare_hits():
    """Sparse hits with long gaps - the clearest way to hear a reverb tail."""
    track = silence(6.0)
    for t, g in [(0.05, 0.9), (1.55, 0.9), (3.05, 0.7), (3.3, 0.9)]:
        place(track, snare(), t, g)
    return track, track.copy()


# ---- plucked guitar (Karplus-Strong with a simple body resonance) -------------
def pluck(freq, seconds=2.2, brightness=0.5):
    n = int(seconds * SR)
    period = SR / freq
    p = int(period)
    buf = lowpass(rng.uniform(-1, 1, p), 2000 + 6000 * brightness)
    out = np.zeros(n)
    for i in range(n):
        out[i] = buf[i % p]
        nxt = buf[(i + 1) % p]
        buf[i % p] = 0.996 * ((1 - 0.5) * buf[i % p] + 0.5 * nxt)
    body = bandpass(out, 90, 5000) + 0.3 * bandpass(out, 180, 260)
    return body * env(n, 1.4, 0.0005)


def guitar():
    # Am - F - C - G arpeggios
    chords = [[110.0, 164.81, 220.0, 261.63, 329.63],
              [87.31, 130.81, 174.61, 220.0, 261.63],
              [130.81, 196.0, 261.63, 329.63, 392.0],
              [98.0, 146.83, 196.0, 246.94, 293.66]]
    step = 0.21
    L = silence(len(chords) * 6 * step + 2.0)
    R = L.copy()
    t = 0.02
    for chord in chords:
        for j, note in enumerate(chord + [chord[2]]):
            s = pluck(note, 1.8, 0.4 + 0.1 * (j % 2))
            pan = 0.35 + 0.3 * (j / 5)
            place(L, s, t, 0.5 * (1 - pan) * 2)
            place(R, s, t, 0.5 * pan * 2)
            t += step
    return L, R


# ---- electric piano (two-operator FM, Rhodes-like) -----------------------------
def ep_note(freq, seconds=2.5, vel=0.8):
    n = int(seconds * SR)
    t = np.arange(n) / SR
    mod_index = 2.2 * vel * np.exp(-t / 0.25)
    mod = np.sin(2 * np.pi * freq * t)
    tone = np.sin(2 * np.pi * freq * t + mod_index * mod)
    tine = 0.15 * np.sin(2 * np.pi * freq * 7 * t) * np.exp(-t / 0.05)
    return (tone + tine) * env(n, 1.1, 0.002) * vel


def keys():
    progression = [([220.0, 261.63, 329.63, 392.0], 0.0),     # Am7
                   ([174.61, 220.0, 261.63, 329.63], 1.5),    # Fmaj7
                   ([196.0, 246.94, 293.66, 349.23], 3.0),    # G7
                   ([164.81, 207.65, 246.94, 293.66], 4.5)]   # E7
    L = silence(7.5)
    R = L.copy()
    for notes, start in progression:
        for k, f in enumerate(notes):
            s = ep_note(f, 2.4, 0.55 + 0.1 * (k % 2))
            pan = 0.3 + 0.4 * k / 3
            place(L, s, start + 0.012 * k, 0.32 * (1 - pan) * 2)
            place(R, s, start + 0.012 * k, 0.32 * pan * 2)
        place(L, ep_note(notes[0] / 2, 1.4, 0.7), start, 0.3)
        place(R, ep_note(notes[0] / 2, 1.4, 0.7), start, 0.3)
    return L, R


def write(path, left, right, peak_db=-3.0, edge_fade=True):
    stereo = np.stack([left, right], axis=1)
    stereo *= 10 ** (peak_db / 20) / (np.max(np.abs(stereo)) + 1e-12)
    # 5 ms fade in/out so loops never click
    if edge_fade:
        f = int(0.005 * SR)
        ramp = np.linspace(0, 1, f)[:, None]
        stereo[:f] *= ramp
        stereo[-f:] *= ramp[::-1]
    wavfile.write(path, SR, (stereo * 32767).astype(np.int16))
    print("wrote", path)


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "samples"
    os.makedirs(out, exist_ok=True)
    write(os.path.join(out, "drums.wav"), *drums(), edge_fade=False)
    write(os.path.join(out, "guitar.wav"), *guitar())
    write(os.path.join(out, "keys.wav"), *keys())
    write(os.path.join(out, "snare.wav"), *snare_hits())
