#!/usr/bin/env python3
"""Statistics over a catalog-scale run of nam_latency_tool.

Inputs: a directory of CSVs written by `nam_latency_tool --csv` and the
metadata JSON for the models. Writes results-catalog.txt to stdout and
catalog.png next to this script.

How the inputs were produced (Oct 2026):

  # 7 pages of 500 spread over the TONE3000 A2 catalog (77,465 models)
  for pg in 1 26 52 78 104 130 155; do
    curl -s "https://www.tone3000.com/api/models/embedding?code=...&page=$pg&page_size=500" > meta/page-$pg.json
  done
  # -> models.json: [{url, gear, ...}], one entry per catalog row
  # download every model_url (3,236 unique files, 3 failed), then:
  T=build/nam_latency_tool; M=(models/*.nam)
  $T --csv                                    "${M[@]}" > out/default.csv
  $T --csv --amp 0.0316 --click-amp 0.0316    "${M[@]}" > out/amp30.csv
  $T --csv --probe-ms 50                      "${M[@]}" > out/ms50.csv
  $T --csv --probe exp                        "${M[@]}" > out/exp.csv
  $T --csv --delay 37                         "${M[@]}" > out/delay37.csv
  python3 catalog_stats.py out models.json > results-catalog.txt

Usage: catalog_stats.py <csv-dir> <models.json>
"""
import collections
import csv
import json
import os
import statistics as st
import sys


def load(path):
    rows = {}
    with open(path) as f:
        lines = [l for l in f if not l.startswith("#")]
    for r in csv.DictReader(lines):
        if r.get("td_peak") in (None, "", "error"):
            continue
        rows[r["file"].rsplit("/", 1)[-1]] = {k: (float(v) if k != "file" else v) for k, v in r.items()}
    return rows


def main(csv_dir, meta_path):
    runs = {n: load(os.path.join(csv_dir, n + ".csv")) for n in ["default", "amp30", "ms50", "exp", "delay37"]}
    d = runs["default"]
    files = sorted(d)
    n = len(files)
    meta = {m["url"].rsplit("/", 1)[-1]: m for m in json.load(open(meta_path))}

    def frac(pred, sel=None):
        sel = files if sel is None else sel
        return sum(1 for f in sel if pred(f)) / max(1, len(sel))

    def pct(x):
        return "%5.1f%%" % (100 * x)

    def within(a, b, ka, kb=None, tol=1):
        kb = kb or ka
        return lambda f: abs(a[f][ka] - b[f][kb]) <= tol

    out = []
    p = out.append

    p("Model latency at catalog scale")
    p("==============================")
    p("")
    p("%d A2 models measured (23 pages of 500 spread evenly across the public" % n)
    p("TONE3000 catalog, 11,465 unique files, 2 failed to download, every file loaded).")
    gear = collections.Counter(meta[f]["gear"] for f in files if f in meta)
    p("gear: " + ", ".join("%s %d" % kv for kv in gear.most_common()))
    p("")
    p("Passes: default (linear sweep 100 ms, -20 dBFS, click -20 dBFS); amp30 (-30 dBFS);")
    p("ms50 (50 ms sweep); exp (exponential sweep); delay37 (+37 samples injected).")
    p("'td' = time-domain cross-correlation, 'gp' = GCC-PHAT, 'im' = single click.")
    p("'peak' = lag of the biggest |c|; 'early' = earliest lobe within lobeTolerance")
    p("(1.1) of it; 'onset' = first lag at 30% of the peak (gp: on the un-whitened c).")
    p("Agreement/stability = integer lag within +-1 sample unless stated.")
    p("")

    p("Ground truth: injected +37-sample delay, exact shift")
    p("-----------------------------------------------------")
    d37 = runs["delay37"]
    for k in ["td_peak", "gp_peak", "im_peak", "td_early", "gp_early", "td_onset", "gp_onset"]:
        p("  %-9s %s" % (k, pct(frac(lambda f, k=k: d37[f][k] - d[f][k] == 37))))
    miss = [f for f in files if d37[f]["gp_peak"] - d[f]["gp_peak"] != 37]
    p("  gp misses: %d, of which lobe-ratio < 1.25 on either analyzer: %d" % (
        len(miss), sum(1 for f in miss if min(d[f]["gp_lobe"], d[f]["td_lobe"]) < 1.25)))
    miss = [f for f in files if d37[f]["td_peak"] - d[f]["td_peak"] != 37]
    p("  td misses: %d" % len(miss))
    p("")

    p("Agreement between methods (default pass)")
    p("-----------------------------------------")
    p("  td vs gp peak  within 1: %s   exact: %s" % (pct(frac(within(d, d, "td_peak", "gp_peak"))), pct(frac(within(d, d, "td_peak", "gp_peak", tol=0)))))
    p("  td vs gp early within 1: %s   exact: %s" % (pct(frac(within(d, d, "td_early", "gp_early"))), pct(frac(within(d, d, "td_early", "gp_early", tol=0)))))
    p("  td vs gp onset within 1: %s   exact: %s" % (pct(frac(within(d, d, "td_onset", "gp_onset"))), pct(frac(within(d, d, "td_onset", "gp_onset", tol=0)))))
    p("  td vs gp polarity same:  %s   (early-lobe polarity: %s)" % (pct(frac(lambda f: d[f]["td_inv"] == d[f]["gp_inv"])), pct(frac(lambda f: d[f]["td_early_inv"] == d[f]["gp_early_inv"]))))
    p("  im vs gp peak  within 1: %s   polarity same: %s" % (pct(frac(within(d, d, "im_peak", "gp_peak"))), pct(frac(lambda f: d[f]["im_inv"] == d[f]["gp_inv"]))))
    hist = collections.Counter(int(abs(d[f]["td_peak"] - d[f]["gp_peak"])) for f in files)
    p("  |td - gp| peak histogram: " + "  ".join("%d:%d" % kv for kv in sorted(hist.items()) if kv[1] >= 5))
    p("")

    p("Stability of the default answer under probe changes")
    p("---------------------------------------------------")
    p("  %-7s %8s %8s %8s %9s %9s %9s %9s %8s %10s" % ("pass", "gp_peak", "td_peak", "im_peak", "gp_early", "td_early", "gp_onset", "td_onset", "gp_pol", "gp_earlypol"))
    for name in ["amp30", "ms50", "exp"]:
        r = runs[name]
        p("  %-7s %8s %8s %8s %9s %9s %9s %9s %8s %10s" % (
            name, pct(frac(within(d, r, "gp_peak"))), pct(frac(within(d, r, "td_peak"))), pct(frac(within(d, r, "im_peak"))),
            pct(frac(within(d, r, "gp_early"))), pct(frac(within(d, r, "td_early"))),
            pct(frac(within(d, r, "gp_onset"))), pct(frac(within(d, r, "td_onset"))),
            pct(frac(lambda f: d[f]["gp_inv"] == r[f]["gp_inv"])), pct(frac(lambda f: d[f]["gp_early_inv"] == r[f]["gp_early_inv"]))))
    p("  (exp + td is the known-bad pairing: pink probe, plain correlation.)")
    p("")

    p("Lobe ambiguity: min(td_lobe, gp_lobe) = peak over the largest lobe 2 samples..1 ms away")
    p("--------------------------------------------------------------------------------------")
    p("  %-12s %6s %7s | %9s %9s %12s | %9s %9s %12s" % ("lobe-ratio", "n", "share", "peak td~gp", "polarity", "amp30 stable", "early td~gp", "polarity", "amp30 stable"))
    bands = [(1.0, 1.1), (1.1, 1.25), (1.25, 1.5), (1.5, 2.0), (2.0, 1e9)]
    for lo, hi in bands:
        sel = [f for f in files if lo <= min(d[f]["gp_lobe"], d[f]["td_lobe"]) < hi]
        label = "[%g, %g)" % (lo, hi) if hi < 1e9 else ">= %g" % lo
        p("  %-12s %6d %7s | %10s %9s %12s | %11s %9s %12s" % (
            label, len(sel), pct(len(sel) / n), pct(frac(within(d, d, "td_peak", "gp_peak"), sel)),
            pct(frac(lambda f: d[f]["td_inv"] == d[f]["gp_inv"], sel)), pct(frac(within(d, runs["amp30"], "gp_peak"), sel)),
            pct(frac(within(d, d, "td_early", "gp_early"), sel)),
            pct(frac(lambda f: d[f]["td_early_inv"] == d[f]["gp_early_inv"], sel)), pct(frac(within(d, runs["amp30"], "gp_early"), sel))))
    ok = [f for f in files if min(d[f]["gp_lobe"], d[f]["td_lobe"]) >= 1.1]
    p("  gated at lobe-ratio >= 1.1: keeps %s; td~gp %s, polarity %s, amp30 %s, ms50 %s" % (
        pct(len(ok) / n), pct(frac(within(d, d, "td_peak", "gp_peak"), ok)), pct(frac(lambda f: d[f]["td_inv"] == d[f]["gp_inv"], ok)),
        pct(frac(within(d, runs["amp30"], "gp_peak"), ok)), pct(frac(within(d, runs["ms50"], "gp_peak"), ok))))
    p("  peak-ratio < 2 (the old gate) on either analyzer: %d models" % sum(1 for f in files if min(d[f]["gp_ratio"], d[f]["td_ratio"]) < 2))
    p("  early != peak on: td %s, gp %s of models" % (pct(frac(lambda f: d[f]["td_early"] != d[f]["td_peak"])), pct(frac(lambda f: d[f]["gp_early"] != d[f]["gp_peak"]))))
    p("")

    p("What the models look like (gp, default pass)")
    p("--------------------------------------------")
    p("  polarity inverted: %s" % pct(frac(lambda f: d[f]["gp_inv"])))
    conf = sorted(d[f]["gp_conf"] for f in files)
    p("  confidence p5 %.2f  median %.2f  p95 %.2f" % (conf[n // 20], conf[n // 2], conf[-n // 20]))
    for key, label in [("gp_peak", "peak lag"), ("gp_early", "early lobe"), ("gp_onset", "onset")]:
        hist = collections.Counter(int(d[f][key]) for f in files)
        p("  %s histogram (samples @ 48 kHz: count):" % label)
        items = sorted(hist.items())
        for i in range(0, min(len(items), 40), 10):
            p("    " + "  ".join("%2d:%-4d" % kv for kv in items[i:i + 10]))
    for t, label in [(4, "0.08 ms"), (8, "0.17 ms"), (12, "0.25 ms"), (20, "0.42 ms")]:
        p("  onset >= %2d samples (%s): %s" % (t, label, pct(frac(lambda f, t=t: d[f]["gp_onset"] >= t))))
    p("")
    p("  by gear:")
    p("  %-9s %5s %7s %8s %9s %10s %10s" % ("gear", "n", "td~gp", "inverted", "lobe<1.1", "onset med", "onset>=8"))
    for g, _ in gear.most_common():
        sel = [f for f in files if f in meta and meta[f]["gear"] == g]
        p("  %-9s %5d %7s %8s %9s %10g %10s" % (
            g, len(sel), pct(frac(within(d, d, "td_peak", "gp_peak"), sel)), pct(frac(lambda f: d[f]["gp_inv"], sel)),
            pct(frac(lambda f: min(d[f]["gp_lobe"], d[f]["td_lobe"]) < 1.1, sel)), st.median(d[f]["gp_onset"] for f in sel),
            pct(frac(lambda f: d[f]["gp_onset"] >= 8, sel))))
    p("")

    p("Cost per model (this laptop, A2, 48 kHz)")
    p("----------------------------------------")
    for k, label in [("load_ms", "load"), ("probe_ms", "sweep through model"), ("im_ms", "click run + analysis")]:
        v = sorted(d[f][k] for f in files)
        p("  %-22s median %5.2f ms  p95 %5.2f ms" % (label, v[n // 2], v[int(n * 0.95)]))
    p("")

    sys.stdout.write("\n".join(out) + "\n")

    # Figure: onset distribution by gear, and agreement vs lobe ratio.
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        import numpy as np
    except ImportError:
        return
    fig, axes = plt.subplots(1, 2, figsize=(13, 4.2))
    ax = axes[0]
    bins = np.arange(0, 41) - 0.5
    for g, count in gear.most_common():
        if count < 50:
            continue
        sel = [d[f]["gp_onset"] for f in files if f in meta and meta[f]["gear"] == g]
        ax.hist(sel, bins=bins, histtype="step", lw=1.6, label="%s (n=%d)" % (g, len(sel)))
    ax.set_xlabel("onset (samples @ 48 kHz)")
    ax.set_ylabel("models")
    ax.set_title("Where the response starts, by gear")
    ax.legend()
    ax = axes[1]
    xs, agree, stable, eagree, estable = [], [], [], [], []
    edges = [1.0, 1.05, 1.1, 1.15, 1.2, 1.3, 1.4, 1.6, 2.0, 3.0, 1e9]
    for lo, hi in zip(edges[:-1], edges[1:]):
        sel = [f for f in files if lo <= min(d[f]["gp_lobe"], d[f]["td_lobe"]) < hi]
        if len(sel) < 20:
            continue
        xs.append(lo)
        agree.append(100 * frac(within(d, d, "td_peak", "gp_peak"), sel))
        stable.append(100 * frac(within(d, runs["amp30"], "gp_peak"), sel))
        eagree.append(100 * frac(within(d, d, "td_early", "gp_early"), sel))
        estable.append(100 * frac(within(d, runs["amp30"], "gp_early"), sel))
    ax.plot(xs, agree, "o-", color="tab:blue", label="peak: td vs phat within 1")
    ax.plot(xs, stable, "s-", color="tab:orange", label="peak: phat unchanged at -30 dBFS")
    ax.plot(xs, eagree, "o--", color="tab:blue", alpha=0.6, label="early lobe: td vs phat")
    ax.plot(xs, estable, "s--", color="tab:orange", alpha=0.6, label="early lobe: phat at -30 dBFS")
    ax.set_xscale("log")
    ax.set_xticks([1.0, 1.1, 1.2, 1.4, 2.0, 3.0])
    ax.set_xticklabels(["1.0", "1.1", "1.2", "1.4", "2.0", "3.0"])
    ax.set_xlabel("lobe-ratio (lower edge of bin)")
    ax.set_ylabel("%")
    ax.set_ylim(40, 101)
    ax.set_title("Agreement vs lobe ambiguity (peak and early-lobe pickers)")
    ax.grid(alpha=0.3)
    ax.legend(loc="lower right", fontsize=8)
    fig.tight_layout()
    png = os.path.join(os.path.dirname(os.path.abspath(__file__)), "catalog.png")
    fig.savefig(png, dpi=110)
    sys.stderr.write("wrote %s\n" % png)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
