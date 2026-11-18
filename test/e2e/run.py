#!/usr/bin/env python3
"""Runs the Ardour e2e scenarios headless through luasession.

  test/e2e/run.py [--runs N] [--plugin-dir DIR] [--keep] [scenario ...]

Each run gets a fresh HOME (so fresh Ardour and plugin config) and a fresh
session dir under one temp root, which is deleted afterwards. A scenario
prints PASS/FAIL/METRIC lines and DONE at the end; a run passes when it has
at least one PASS, no FAIL and the DONE.
"""
import argparse
import glob
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
SCENARIOS = {"smoke": ["LV2", "VST3"], "state_roundtrip": ["VST3"]}
TIMEOUT = 240
MARKER = re.compile(r"^(PASS|FAIL|METRIC|DONE)\b ?(.*)$")


def find_ardour():
    found = [(int(m.group(1)), d) for d in glob.glob("/usr/lib/ardour*") + glob.glob("/usr/local/lib/ardour*")
             if (m := re.search(r"ardour(\d+)$", d)) and os.access(os.path.join(d, "luasession"), os.X_OK)]
    return max(found, default=None)


def run_once(name, fmt, run_dir, ardour, args):
    ver, lib = ardour
    home = os.path.join(run_dir, "home")
    scripts = os.path.join(home, ".config", f"ardour{ver}", "scripts")
    os.makedirs(scripts)
    shutil.copy(os.path.join(HERE, "lua", "player.lua"), scripts)
    env = dict(os.environ,
               HOME=home, XDG_CONFIG_HOME=os.path.join(home, ".config"),
               ARDOUR_DLL_PATH=lib, ARDOUR_DATA_PATH=f"/usr/share/ardour{ver}",
               ARDOUR_CONFIG_PATH=f"/etc/ardour{ver}",
               LD_LIBRARY_PATH=lib + os.pathsep + os.environ.get("LD_LIBRARY_PATH", ""),
               LV2_PATH=os.path.join(args.plugin_dir, "LV2"),
               VST3_PATH=os.path.join(args.plugin_dir, "VST3"),
               T3K_E2E_LIB=os.path.join(HERE, "lua", "lib.lua"),
               T3K_E2E_SESSION_DIR=os.path.join(run_dir, "session"),
               T3K_E2E_FORMAT=fmt)
    if fmt == "VST3":
        # luasession only reads the VST3 cache; it never runs the scanner itself.
        subprocess.run([os.path.join(lib, "ardour-vst3-scanner"), "-q", "-f",
                        os.path.join(args.plugin_dir, "VST3", "TONE3000.vst3")],
                       env=env, cwd=run_dir, capture_output=True, timeout=TIMEOUT)
    cmd = [os.path.join(lib, "luasession"), os.path.join(HERE, "scenarios", name + ".lua")]
    try:
        out = subprocess.run(cmd, env=env, cwd=run_dir, capture_output=True, text=True,
                             errors="replace", timeout=TIMEOUT)
        out = out.stdout + out.stderr
    except subprocess.TimeoutExpired as e:
        out = (e.stdout or b"").decode(errors="replace") + "\nTIMEOUT"

    marks = [m.groups() for line in out.splitlines() if (m := MARKER.match(line.strip()))]
    fails = [rest for kind, rest in marks if kind == "FAIL"]
    metrics = dict(rest.partition("=")[::2] for kind, rest in marks if kind == "METRIC")
    ok = any(k == "PASS" for k, _ in marks) and not fails and ("DONE", "") in marks
    print(f"  {'PASS' if ok else 'FAIL'}  {name} {fmt}  {metrics}"
          + ("" if ok else f"  {fails or 'no DONE marker'}"))
    if not ok:
        print("\n".join(out.splitlines()[-25:]))
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("scenarios", nargs="*", default=list(SCENARIOS))
    ap.add_argument("--runs", type=int, default=1)
    ap.add_argument("--plugin-dir", default=os.path.join(
        REPO, "build", "plugin", "TONE3000_artefacts", "Debug"))
    ap.add_argument("--keep", action="store_true", help="keep the temp root for debugging")
    args = ap.parse_args()
    args.plugin_dir = os.path.abspath(args.plugin_dir)

    ardour = find_ardour()
    if not ardour:
        print("SKIP  no Ardour luasession found under /usr/lib or /usr/local/lib")
        return 0
    print(f"Ardour {ardour[0]} at {ardour[1]}")

    root = tempfile.mkdtemp(prefix="t3k-e2e-")
    failed = 0
    try:
        for name in args.scenarios:
            for fmt in SCENARIOS[name]:
                for i in range(args.runs):
                    run_dir = os.path.join(root, f"{name}-{fmt}-{i}")
                    failed += not run_once(name, fmt, run_dir, ardour, args)
    finally:
        if args.keep:
            print(f"kept {root}")
        else:
            shutil.rmtree(root, ignore_errors=True)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
