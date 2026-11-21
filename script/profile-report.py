#!/usr/bin/env python3
"""Report on a stage-profiler recording (see plugin/include/StageProfiler.h).

  cmake -B build-prof -DCMAKE_BUILD_TYPE=Release -DT3K_PROFILE=ON
  cmake --build build-prof --target TONE3000_Standalone
  T3K_PROFILE_OUT=/tmp/rec.csv build-prof/plugin/TONE3000_artefacts/Release/Standalone/TONE3000
  script/profile-report.py /tmp/rec.csv

Any host works, the plugin records whenever T3K_PROFILE_OUT is set.

Per callback size: block time against the deadline, deadline misses and the
time each stage took per block (inclusive, so Chain contains its blocks).
"""
import collections
import sys


def pct(vals, p):
    return vals[round(p * (len(vals) - 1))]


def main():
    blocks, stages = [], collections.defaultdict(collections.Counter)
    for line in open(sys.argv[1]):
        f = line.rstrip("\n").split(",")
        if f[0] == "B":
            blocks.append(dict(block=int(f[1]), dur=int(f[2]), frames=int(f[3]), deadline=int(f[4])))
        elif f[0] == "S":
            stages[int(f[1])][f[2] + (f"#{f[3]}" if f[3] != "-1" else "")] += int(f[4])

    by_size = collections.defaultdict(list)
    for b in blocks:
        by_size[b["frames"]].append(b)
    for frames, bl in sorted(by_size.items()):
        deadline = bl[0]["deadline"] / 1e3
        durs = sorted(b["dur"] / 1e3 for b in bl)
        misses = sum(b["dur"] > b["deadline"] for b in bl)
        print(f"== {frames} frames, deadline {deadline:.1f} us, {len(bl)} blocks, "
              f"{misses} misses ({100 * misses / len(bl):.2f}%)")
        print(f"   block us  p50 {pct(durs, .5):.1f}  p99 {pct(durs, .99):.1f}  "
              f"p99.9 {pct(durs, .999):.1f}  max {durs[-1]:.1f}")
        series = collections.defaultdict(list)
        for b in bl:
            for stage, ns in stages[b["block"]].items():
                series[stage].append(ns / 1e3)
        print(f"   {'stage (us per block)':24s} {'blocks':>7s} {'mean':>8s} {'p99.9':>8s} {'max':>8s}")
        for stage, v in sorted(series.items(), key=lambda kv: -sum(kv[1]) / len(kv[1])):
            v.sort()
            print(f"   {stage:24s} {len(v):7d} {sum(v) / len(v):8.1f} {pct(v, .999):8.1f} {v[-1]:8.1f}")
        print()


if __name__ == "__main__":
    main()
