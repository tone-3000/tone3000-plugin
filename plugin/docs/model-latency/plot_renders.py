#!/usr/bin/env python3
"""Plot the first pick attack of each *-aligned.wav render from nam_latency_tool --di.

Each render has L = model output, R = DI delayed by the measured latency
(polarity corrected). The plot also overlays the DI *as played* (R shifted
back) so the measured delay is visible as the gap between the dashed and
solid blue traces. The delay per file is read from the results-*.txt files
next to this script (the "DI render:" lines).

    python3 plugin/docs/model-latency/plot_renders.py plugin/docs/model-latency/renders/*.wav
"""
import os
import sys
import wave

import matplotlib
import numpy as np

matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))


def read24(path):
    w = wave.open(path)
    assert w.getsampwidth() == 3 and w.getnchannels() == 2
    raw = w.readframes(w.getnframes())
    b = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3)
    s = b[:, 0].astype(np.int32) | (b[:, 1].astype(np.int32) << 8) | (b[:, 2].astype(np.int32) << 16)
    s = np.where(s & 0x800000, s - (1 << 24), s) / 8388608.0
    return s.reshape(-1, 2), w.getframerate()


def read_delays():
    delays = {}
    for results in sorted(os.listdir(HERE)):
        if not (results.startswith("results-") and results.endswith(".txt")):
            continue
        with open(os.path.join(HERE, results)) as f:
            for line in f:
                if "DI render:" in line:
                    name = line.split("DI render: ")[1].split(" ")[0].split("/")[-1]
                    d = int(line.split("delayed ")[1].split(" ")[0])
                    delays[name] = (d, "inverted" in line)
    return delays


def first_attack(x, sr):
    """Index of the first pick attack. Clips that open with silence (the
    neural-amp-modeler-wasm inputs do): the first sample above 1% of peak.
    Otherwise: where the 1 ms RMS envelope first jumps to > 4x its level
    20 ms earlier."""
    peak = np.max(np.abs(x))
    first = int(np.argmax(np.abs(x) > 0.01 * peak))
    if first > int(0.05 * sr):
        return first
    w, look = int(0.001 * sr), int(0.02 * sr)
    env = np.sqrt(np.convolve(x * x, np.ones(w) / w, mode="same"))
    for i in range(look, len(x)):
        if env[i] > 0.1 * peak and env[i] > 4.0 * env[i - look]:
            return i
    return first


def main(files):
    delays = read_delays()
    fig, axes = plt.subplots(len(files), 1, figsize=(11, 2.2 * len(files)))
    if len(files) == 1:
        axes = [axes]
    for ax, path in zip(axes, files):
        data, sr = read24(path)
        out, ref = data[:, 0], data[:, 1]
        name = os.path.basename(path)
        d, inv = delays.get(name, (0, False))
        n = 240
        i0 = max(first_attack(ref, sr) - 60, d)
        t = np.arange(n)
        # Each trace normalized within the window (the first note of a clip
        # is often far below the clip's peak).
        def local(v):
            return v / max(np.max(np.abs(v)), 1e-9)
        dry = (-1 if inv else 1) * ref[i0 + d:i0 + d + n]  # undo delay/polarity: the DI as played
        ax.plot(t, local(dry), "--", color="0.6", lw=1, label="DI as played")
        ax.plot(t, local(ref[i0:i0 + n]), color="tab:blue", lw=1.2,
                label=f"DI delayed {d} smp{' + inverted' if inv else ''}")
        ax.plot(t, local(out[i0:i0 + n]), color="tab:red", lw=1.2, label="model output")
        ax.set_title(f"{name.replace('-aligned.wav', '')}  (first attack, {n} samples @ {sr} Hz)",
                     fontsize=10, loc="left")
        ax.legend(loc="upper right", fontsize=8, ncol=3)
        ax.set_yticks([])
        ax.grid(alpha=0.3)
    axes[-1].set_xlabel("samples")
    plt.tight_layout()
    out_path = os.path.join(HERE, "renders.png")
    plt.savefig(out_path, dpi=110)
    print(f"wrote {out_path}")


if __name__ == "__main__":
    main(sys.argv[1:])
