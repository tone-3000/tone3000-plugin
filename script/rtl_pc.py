#!/usr/bin/env python3
"""PC-rig ground-truth round-trip latency (L2, plan section 4 "L2").

The PC is both source and recorder, so its own latency cancels:

    burst out ──> PC out ──┬──(loopback cable)─────> PC in ch1  [reference]
                           └──> tablet/AMS-22 IN ... AMS-22 OUT ─> PC in ch2

Each trial plays one probe burst and records two channels, finds the burst in
each channel by cross-correlation against the generated probe, and reports the
argmax difference as the tablet path's round-trip latency:

    rtlSamples = argmax(xcorr(ch2, probe)) - argmax(xcorr(ch1, probe))

Prints per-trial rows, the median, and a peak/next-lobe ratio sanity metric
(< 3.0 means the measurement is garbage — noise floor or wrong channel).

Usage:
    ./script/rtl_pc.py --list-devices
    ./script/rtl_pc.py --trials 20 --rate 48000
    ./script/rtl_pc.py --out-device 2 --in-device 1 --csv results.txt

Needs python-sounddevice and numpy (pip install sounddevice numpy).
"""

import argparse
import sys
import time

import numpy as np
import sounddevice as sd


def make_probe(kind: str, rate: int, burst_ms: float, amp_db: float) -> np.ndarray:
    """Deterministic probe burst: 2 ms exponential sine sweep (default) or an
    MLS sequence, at amp_db FS. The sweep's autocorrelation is a sharp peak,
    which makes the argmax robust against the device's band-limited response."""
    n = max(1, int(rate * burst_ms / 1000.0))
    amp = 10.0 ** (amp_db / 20.0)
    if kind == "sweep":
        # Exponential sine sweep f0..f1 over the burst, with short fade
        # in/out so the click itself does not dominate the spectrum.
        t = np.arange(n) / rate
        f0, f1 = 200.0, 0.4 * rate
        k = n / (rate * np.log(f1 / f0))
        sweep = np.sin(2.0 * np.pi * f0 * k * (np.exp(t / k) - 1.0))
        fade = np.minimum(1.0, np.minimum(np.arange(n), n - 1 - np.arange(n)) / (0.001 * rate))
        return (amp * sweep * fade).astype(np.float32)
    if kind == "mls":
        # Length-4096 maximum-length sequence (x^12 + x^6 + x^4 + x + 1),
        # deterministic seed state.
        length = 4096
        reg = 0xFFF
        bits = []
        for _ in range(length):
            bit = ((reg >> 11) ^ (reg >> 5) ^ (reg >> 3) ^ (reg >> 0)) & 1
            reg = ((reg << 1) | bit) & 0xFFF
            bits.append(bit)
        mls = np.array(bits, dtype=np.float32) * 2.0 - 1.0
        return (amp * mls).astype(np.float32)
    raise ValueError(f"unknown probe kind: {kind}")


def find_burst(rec: np.ndarray, probe: np.ndarray) -> tuple[int, float]:
    """Return (argmax lag, peak/next-lobe ratio) of rec cross-correlated with
    the probe. Lag is in samples of rec."""
    corr = np.correlate(rec, probe, mode="full")
    peak_idx = int(np.argmax(np.abs(corr)))
    peak = float(np.abs(corr[peak_idx]))
    # Next lobe: the largest correlation value at least one burst length away
    # from the peak (so the main lobe's own skirt does not count).
    guard = len(probe)
    masked = np.abs(corr).copy()
    lo = max(0, peak_idx - guard)
    hi = min(len(masked), peak_idx + guard)
    masked[lo:hi] = 0.0
    next_lobe = float(masked.max()) if masked.size else 0.0
    ratio = peak / next_lobe if next_lobe > 0 else float("inf")
    lag = peak_idx - (len(probe) - 1)
    return lag, ratio


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--list-devices", action="store_true")
    ap.add_argument("--out-device", default=None, help="output device index or name")
    ap.add_argument("--in-device", default=None, help="input device index or name")
    ap.add_argument("--rate", type=int, default=48000)
    ap.add_argument("--trials", type=int, default=20)
    ap.add_argument("--probe", choices=("sweep", "mls"), default="sweep")
    ap.add_argument("--burst-ms", type=float, default=2.0)
    ap.add_argument("--amp-db", type=float, default=-12.0)
    ap.add_argument("--gap-ms", type=float, default=150.0, help="silence between trials")
    ap.add_argument("--ref-channel", type=int, default=0, help="rec ch with the loopback reference")
    ap.add_argument("--path-channel", type=int, default=1, help="rec ch with the tablet path")
    ap.add_argument("--in-channels", type=int, default=2, help="input channels to record")
    ap.add_argument("--out-channels", default="all",
                    help="comma-separated output channel indices to play the burst on "
                         "(default: all; e.g. '2,3' keeps monitors on 0,1 silent)")
    ap.add_argument("--csv", help="append per-trial rows to this file")
    ap.add_argument("--window-ms", default=None,
                    help="lo,hi: pick the strongest path peak in this window after the "
                         "reference instead of the global argmax. Needed because the "
                         "AMS-22 feeds its input to its output at ~1 ms regardless of "
                         "its monitor switches; that hardware peak is stronger than "
                         "the app path and would win the argmax.")
    args = ap.parse_args()

    # Accept numeric strings as indices, everything else as a device name.
    for attr in ("out_device", "in_device"):
        v = getattr(args, attr)
        if v is not None and str(v).isdigit():
            setattr(args, attr, int(v))

    if args.list_devices:
        print(sd.query_devices())
        return 0

    probe = make_probe(args.probe, args.rate, args.burst_ms, args.amp_db)
    gap = np.zeros(int(args.rate * args.gap_ms / 1000.0), dtype=np.float32)
    burst_pad = np.concatenate([gap, probe, gap])
    record_s = burst_pad.size / args.rate + 0.25

    out_ch = 2 if args.out_device is None else int(sd.query_devices(args.out_device)["max_output_channels"])
    in_dev = sd.query_devices(args.in_device, "input") if args.in_device is not None else sd.query_devices(kind="input")
    in_ch = int(in_dev["max_input_channels"])
    if in_ch < args.in_channels or args.in_channels <= args.path_channel:
        print(f"input device has {in_ch} channels; need {args.in_channels} "
              f"(path channel {args.path_channel})", file=sys.stderr)
        return 1

    # Playback the burst on the selected output channels (either the loopback
    # cable or the interface input picks it up); everything else stays silent.
    out = np.zeros((burst_pad.size, max(2, out_ch)), dtype=np.float32)
    if args.out_channels == "all":
        out[:] = burst_pad[:, None]
    else:
        for ch in args.out_channels.split(","):
            out[:, int(ch)] = burst_pad

    results = []
    window = None
    if args.window_ms:
        lo, hi = (float(x) for x in args.window_ms.split(","))
        window = (lo, hi)

    for trial in range(args.trials):
        rec = sd.playrec(out, samplerate=args.rate, channels=args.in_channels,
                         blocking=True, device=(args.in_device, args.out_device))
        rec = np.asarray(rec, dtype=np.float64)
        n = int(args.rate * record_s)
        if rec.shape[0] < n:
            n = rec.shape[0]
        ref = rec[:n, args.ref_channel]
        path = rec[:n, args.path_channel]
        lag_ref, ratio_ref = find_burst(ref, probe)
        if window is None:
            lag_path, ratio_path = find_burst(path, probe)
        else:
            # Restrict the search to the window (ms after the reference): the
            # strongest peak in range is the app path; the ~1 ms hardware
            # monitor feed and the ref-sized crosstalk stay excluded.
            lo_lag = lag_ref + int(window[0] * args.rate / 1000.0)
            hi_lag = lag_ref + int(window[1] * args.rate / 1000.0)
            lo_lag = max(0, min(lo_lag, len(path) - len(probe)))
            hi_lag = max(lo_lag, min(hi_lag, len(path) - len(probe)))
            seg = path[lo_lag:hi_lag + len(probe)]
            seg_lag, seg_ratio = find_burst(seg, probe)
            lag_path = seg_lag + lo_lag
            ratio_path = seg_ratio
        rtl_ms = (lag_path - lag_ref) / args.rate * 1000.0
        results.append((rtl_ms, min(ratio_ref, ratio_path)))
        ts = time.strftime("%Y-%m-%dT%H:%M:%S")
        print(f"[{trial + 1:3d}/{args.trials}] rtl={rtl_ms:8.3f} ms  "
              f"peakRatio ref={ratio_ref:6.2f} path={ratio_path:6.2f}")
        if args.csv:
            with open(args.csv, "a", encoding="utf-8") as f:
                f.write(f"{ts},pc-rig,{args.rate},{args.probe},{rtl_ms:.3f},"
                        f"{min(ratio_ref, ratio_path):.2f}\n")
        sd.wait()

    rtls = sorted(r[0] for r in results)
    median = rtls[len(rtls) // 2]
    worst_ratio = min(r[1] for r in results)
    print(f"\nmedian rtl = {median:.3f} ms over {args.trials} trials  "
          f"(min {rtls[0]:.3f}, max {rtls[-1]:.3f})")
    print(f"worst peak/next-lobe ratio = {worst_ratio:.2f}"
          + ("  [GARBAGE: < 3.0]" if worst_ratio < 3.0 else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
