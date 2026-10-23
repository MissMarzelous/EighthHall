"""
plot_ir.py - analyse an impulse response rendered by the test program.

    build/EighthHallTests --render demo-audio
    python tools/plot_ir.py demo-audio/impulse_response_hall.wav docs/images/ir_analysis.png

Produces a two-panel figure: a spectrogram (showing highs dying faster than
lows - the Damping control at work) and the Schroeder energy decay curve per
octave band with the measured RT60s.

Requires: numpy, scipy, matplotlib   (pip install numpy scipy matplotlib)
"""
import sys

import matplotlib.pyplot as plt
import numpy as np
from scipy.io import wavfile
from scipy.signal import butter, sosfilt, spectrogram

BG, PANEL, GRID, TEXT, DIM = "#0e1015", "#161a22", "#2a303d", "#e6e8ee", "#8a91a3"
BAND_COLOURS = ["#5cc8dc", "#7fd6a6", "#f0d06a", "#f0b46a", "#ef7f6a"]


def edc_db(x):
    energy = np.cumsum((x.astype(np.float64) ** 2)[::-1])[::-1]
    return 10 * np.log10(energy / energy[0] + 1e-30)


def rt60_from_edc(edc, fs):
    idx = np.where((edc <= -5) & (edc >= -35))[0]
    if len(idx) < 10:
        return float("nan")
    slope, _ = np.polyfit(idx / fs, edc[idx], 1)
    return -60.0 / slope


def main(wav_path, out_path):
    fs, data = wavfile.read(wav_path)
    x = data.astype(np.float64)
    if x.ndim == 2:
        x = x.mean(axis=1)
    x /= np.max(np.abs(x)) + 1e-12
    t_max = len(x) / fs

    plt.rcParams.update({"font.size": 10, "text.color": TEXT, "axes.labelcolor": DIM,
                         "xtick.color": DIM, "ytick.color": DIM})
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12, 4.2), facecolor=BG)
    for ax in (ax1, ax2):
        ax.set_facecolor(PANEL)
        for s in ax.spines.values():
            s.set_color(GRID)

    # --- spectrogram ---------------------------------------------------------
    f, t, sxx = spectrogram(x, fs, nperseg=1024, noverlap=896)
    sxx_db = 10 * np.log10(sxx + 1e-14)
    sxx_db -= sxx_db.max()
    mesh = ax1.pcolormesh(t, f / 1000, sxx_db, shading="auto", cmap="magma", vmin=-90, vmax=0)
    ax1.set_ylim(0, 16)
    ax1.set_xlabel("Time (s)")
    ax1.set_ylabel("Frequency (kHz)")
    ax1.set_title("Impulse response spectrogram", color=TEXT, loc="left")
    cb = fig.colorbar(mesh, ax=ax1, pad=0.01)
    cb.set_label("dB", color=DIM)
    cb.ax.tick_params(colors=DIM)

    # --- energy decay per octave band ----------------------------------------
    for colour, centre in zip(BAND_COLOURS, [250, 500, 1000, 2000, 4000]):
        sos = butter(4, [centre / np.sqrt(2), centre * np.sqrt(2)], btype="band", fs=fs, output="sos")
        edc = edc_db(sosfilt(sos, x))
        rt = rt60_from_edc(edc, fs)
        label = f"{centre if centre < 1000 else str(centre // 1000) + 'k'} Hz  RT60 {rt:.2f} s"
        ax2.plot(np.arange(len(edc)) / fs, edc, color=colour, lw=1.6, label=label)

    ax2.axhline(-60, color=DIM, lw=0.8, ls="--")
    ax2.set_ylim(-80, 2)
    ax2.set_xlim(0, t_max)
    ax2.set_xlabel("Time (s)")
    ax2.set_ylabel("Energy decay (dB)")
    ax2.set_title("Schroeder energy decay by octave band", color=TEXT, loc="left")
    ax2.grid(color=GRID, lw=0.6)
    leg = ax2.legend(facecolor=PANEL, edgecolor=GRID, fontsize=9)
    for txt in leg.get_texts():
        txt.set_color(TEXT)

    fig.tight_layout()
    fig.savefig(out_path, dpi=150, facecolor=BG)
    print(f"wrote {out_path}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
