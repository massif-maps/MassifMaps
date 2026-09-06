#!/usr/bin/env python3
"""devtap - read device logs, crashes and frames without dumping them into an agent's context.

Every subcommand does the bulk work on disk and prints a SUMMARY. The full artifact is always
written to --out (default build/devtap/) so it can be grepped later without ever being read whole.

  devtap.py logs   android|ios   filtered + deduplicated log tail
  devtap.py crash  android|ios   crashing thread only, symbolicated
  devtap.py shot   android|ios   screenshot to disk + per-band statistics, never the image
  devtap.py diff   A.png B.png   A/B image comparison as numbers

Only dependency beyond the stdlib is PIL (no numpy on this machine).
"""

import argparse
import glob
import json
import os
import re
import shutil
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUTDIR = os.path.join(ROOT, "build", "devtap")

ANDROID_PKG = "com.massifmaps.MassifDemo"
IOS_PROC = "MassifDemo"

# Tags that carry nothing about this SDK. Dropped before anything is counted.
NOISE_TAGS = {
    "ActivityManager", "ActivityTaskManager", "AudioManager", "BufferQueueProducer",
    "ColorStateList", "ConnectivityManager", "DisplayManager", "EGL_emulation", "GnssHal",
    "GoogleInputMethod", "HostConnection", "InputMethodManager", "InputTransport",
    "InsetsController", "LoadedApk", "MessageQueue", "NetworkController", "OpenGLRenderer",
    "PackageManager", "PowerManagerService", "ResourcesCompat", "ScrollView", "StatusBar",
    "SurfaceControl", "SurfaceView", "Toast", "ViewRootImpl", "WindowManager", "WindowOnBackDispatcher",
    "chatty", "emuglGLESv2_enc", "eglCodecCommon", "gralloc4", "libEGL", "libc", "netmgr",
    "nativeloader", "ziparchive", "zygote", "zygote64", "Choreographer", "Parcel",
    "BpBinder", "BpTransactionCompletedListener", "AdrenoUtils", "qdgralloc", "Gralloc4",
}

# Whole lines matching these are never interesting, whatever the tag.
NOISE_RE = re.compile(
    r"Accessing hidden|Unsupported class loader|Skipped \d+ frames|"
    r"type=1400 audit|Compat change id reported|"
    r"Davey!|Long monitor contention|GC freed|Background concurrent|"
    r"Ignoring header X-Firebase|No package ID"
)

# Numbers, addresses, timestamps and pointers vary per line but carry no new information for
# deduplication. Normalise them away so N repeats of a probe collapse into one line + a count.
VARIABLE_RE = re.compile(r"0x[0-9a-fA-F]+|-?\d+\.\d+|\b\d+\b")

LEVELS = "VDIWEF"


def log(msg):
    print(msg, file=sys.stderr)


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, errors="replace", **kw)


def ensure_outdir(path=None):
    d = path or OUTDIR
    os.makedirs(d, exist_ok=True)
    return d


def stamp():
    return time.strftime("%Y%m%d-%H%M%S")


# --------------------------------------------------------------------------------------- devices

def adb(args, serial=None):
    cmd = ["adb"]
    if serial:
        cmd += ["-s", serial]
    return run(cmd + args)


def pick_android(serial):
    if serial:
        return serial
    env = os.environ.get("ANDROID_SERIAL")
    if env:
        return env
    out = run(["adb", "devices"]).stdout.splitlines()[1:]
    devs = [l.split()[0] for l in out if l.strip().endswith("device")]
    if not devs:
        sys.exit("devtap: no android device. `adb devices`")
    if len(devs) > 1:
        sys.exit("devtap: %d android devices (%s) - pass --device or set ANDROID_SERIAL. "
                 "Several sessions share these emulators; guessing is how runs get crossed."
                 % (len(devs), " ".join(devs)))
    return devs[0]


def pick_ios(udid):
    if udid:
        return udid
    out = run(["xcrun", "simctl", "list", "devices", "booted", "-j"]).stdout
    try:
        data = json.loads(out)
    except json.JSONDecodeError:
        sys.exit("devtap: could not parse `simctl list devices booted -j`")
    booted = [d for ds in data.get("devices", {}).values() for d in ds]
    if not booted:
        sys.exit("devtap: no booted iOS simulator. `xcrun simctl boot <udid>`")
    if len(booted) > 1:
        sys.exit("devtap: %d booted simulators - pass --device" % len(booted))
    return booted[0]["udid"]


# ------------------------------------------------------------------------------------------ logs

LINE_RE = re.compile(r"^[\d\-\s:.]*\s+(\d+)\s+\d+\s+([VDIWEF])\s+([^:]*?)\s*:\s?(.*)$")

# `log show --style compact`: "<ts> Df MassifDemo[1234:5678] [sub:cat] message"
IOS_RE = re.compile(r"^[\d\-\s:.+]{10,}\s+(\w{2,7})\s+([\w.\-]+)\[(\d+):[^\]]*\]\s*(.*)$")
IOS_LEVEL = {"Df": "D", "In": "I", "Dg": "D", "Ac": "I", "Er": "E", "Fa": "F", "De": "I"}


def parse_ios_line(raw):
    m = IOS_RE.match(raw)
    if not m:
        return None
    typ, proc, pid, msg = m.groups()
    # A bracketed "[subsystem:category]" prefix is the useful tag; otherwise use the process.
    tag = proc
    c = re.match(r"^\[([^\]]+)\]\s*(.*)$", msg)
    if c:
        tag, msg = c.group(1), c.group(2)
    return pid, IOS_LEVEL.get(typ[:2], "I"), tag, msg


def filter_lines(lines, keep_re, drop_noise, min_level, pids, ios=False):
    """Return (kept, stats). Each kept entry is (count, level, tag, message, first_raw).

    A stack-trace continuation ("\tat ...", "Caused by:") folds into the entry above it, so a
    40-frame Java trace costs one line plus its own cap instead of 40 deduplicated entries.
    """
    lvl_min = LEVELS.index(min_level.upper()) if min_level else 0
    order = {}          # normalised key -> index into kept
    kept = []
    dropped_noise = dropped_level = dropped_pid = 0

    for raw in lines:
        if ios:
            p = parse_ios_line(raw)
            if not p:
                continue        # header row, blank, or a wrapped continuation of a long payload
            pid, level, tag, msg = p
        else:
            m = LINE_RE.match(raw)
            if m:
                pid, level, tag, msg = m.group(1), m.group(2), m.group(3).strip(), m.group(4)
            else:
                pid, level, tag, msg = "", "I", "", raw.strip()
        if not msg:
            continue

        if keep_re:
            if not keep_re.search(raw):
                continue
        else:
            if pids and pid not in pids:
                dropped_pid += 1
                continue
            if drop_noise and (tag in NOISE_TAGS or NOISE_RE.search(msg)):
                dropped_noise += 1
                continue
            if LEVELS.index(level) < lvl_min:
                dropped_level += 1
                continue

        if kept and re.match(r"^(\tat |\s+at |Caused by:|\.\.\. \d+ more)", msg):
            kept[-1][5].append(msg.strip())
            continue

        key = (tag, VARIABLE_RE.sub("#", msg))
        if key in order:
            kept[order[key]][0] += 1
        else:
            order[key] = len(kept)
            kept.append([1, level, tag, msg, raw, []])

    return kept, dict(noise=dropped_noise, level=dropped_level, pid=dropped_pid)


def emit(kept, stats, total, maxlines, full_path, label, frames):
    collapsed = sum(e[0] for e in kept) - len(kept)
    # Errors first so a cap never truncates away the thing that matters.
    kept.sort(key=lambda e: (-LEVELS.index(e[1]), -e[0]))
    shown = kept[:maxlines]
    print("== %s: %d raw -> %d unique (other pids %d, noise %d, below level %d, collapsed %d dup)"
          % (label, total, len(kept), stats["pid"], stats["noise"], stats["level"], collapsed))
    if full_path:
        print("== full log: %s   (grep it, do not read it)" % os.path.relpath(full_path, ROOT))
    for count, level, tag, msg, _, trace in shown:
        n = " x%d" % count if count > 1 else ""
        print("%s %s%s: %s" % (level, tag or "-", n, msg[:400]))
        for t in trace[:frames]:
            print("    %s" % t[:300])
        if len(trace) > frames:
            print("    ... %d more frames" % (len(trace) - frames))
    if len(kept) > maxlines:
        print("... %d more unique lines, in the full log" % (len(kept) - maxlines))


def app_pids(dev, pkg):
    """PIDs of the app under test. Empty set means 'could not tell' - then do not filter."""
    r = adb(["shell", "pidof", pkg], dev)
    return set(r.stdout.split()) if r.returncode == 0 else set()


def cmd_logs(a):
    out = ensure_outdir(a.out)
    keep_re = re.compile(a.grep) if a.grep else None
    pids = set()

    if a.platform == "android":
        dev = pick_android(a.device)
        if not a.all and not a.tag:
            pids = app_pids(dev, a.package)
            if not pids:
                log("devtap: %s is not running, showing every pid" % a.package)
        cmd = ["logcat", "-d", "-v", "threadtime"]
        if a.since:
            cmd += ["-t", a.since]
        elif a.lines:
            cmd += ["-t", str(a.lines)]
        if a.tag:
            cmd += ["-s"] + a.tag
        r = adb(cmd, dev)
        if r.returncode:
            sys.exit("devtap: adb logcat failed: %s" % r.stderr.strip()[:200])
        text = r.stdout
        label = "logcat %s" % dev
    else:
        dev = pick_ios(a.device)
        pred = a.predicate or ('process == "%s" OR senderImagePath CONTAINS "Massif"' % IOS_PROC)
        r = run(["xcrun", "simctl", "spawn", dev, "log", "show",
                 "--style", "compact", "--last", a.since or "3m", "--predicate", pred])
        if r.returncode:
            sys.exit("devtap: simctl log show failed: %s" % r.stderr.strip()[:200])
        text = r.stdout
        label = "ios log %s" % dev[:8]

    full = os.path.join(out, "log-%s-%s.txt" % (a.platform, stamp()))
    with open(full, "w") as fh:
        fh.write(text)

    lines = text.splitlines()
    kept, stats = filter_lines(lines, keep_re, not a.no_filter, a.level, pids,
                               ios=(a.platform == "ios"))
    emit(kept, stats, len(lines), a.max, full, label, a.frames)


# ---------------------------------------------------------------------------------------- crashes

def newest_ndk():
    ndks = sorted(glob.glob(os.path.expanduser("~/Library/Android/sdk/ndk/*")))
    return ndks[-1] if ndks else None


def find_symbol_dir(explicit):
    """Directory holding the UNSTRIPPED .so, for ndk-stack."""
    if explicit:
        return explicit
    cands = glob.glob(os.path.join(
        ROOT, "scripts/android-dev/massif/build/intermediates/cxx/*/*/obj/arm64-v8a"))
    cands += [os.path.join(ROOT, "build/android-arm64-v8a")]
    cands = [c for c in cands if glob.glob(os.path.join(c, "*.so"))]
    if not cands:
        return None
    return max(cands, key=os.path.getmtime)


def cmd_crash_android(a):
    out = ensure_outdir(a.out)
    dev = pick_android(a.device)
    r = adb(["logcat", "-b", "crash", "-d", "-v", "threadtime"], dev)
    text = r.stdout
    if not text.strip():
        r = adb(["logcat", "-d", "-v", "threadtime", "-t", "4000"], dev)
        text = r.stdout

    full = os.path.join(out, "crash-android-%s.txt" % stamp())
    with open(full, "w") as fh:
        fh.write(text)

    lines = text.splitlines()

    # Java crash: the FATAL EXCEPTION block, own frames first.
    jstart = None
    for i, l in enumerate(lines):
        if "FATAL EXCEPTION" in l:
            jstart = i
    if jstart is not None:
        block = lines[jstart:jstart + 120]
        print("== java crash (%s)" % os.path.relpath(full, ROOT))
        printed = 0
        for l in block:
            msg = l.split(": ", 1)[-1] if ": " in l else l
            if msg.strip().startswith("at ") and "com.massifmaps" not in msg and printed > 3:
                continue
            print(msg.rstrip()[:300])
            printed += 1
            if printed >= a.max:
                break
        return

    # Native crash: hand the tombstone to ndk-stack, then keep the crashing thread only.
    if "*** *** ***" not in text and "signal " not in text:
        print("== no crash found in the crash buffer or the last 4000 logcat lines")
        print("== full log: %s" % os.path.relpath(full, ROOT))
        return

    symdir = find_symbol_dir(a.symbols)
    ndk = newest_ndk()
    body = text
    if symdir and ndk and os.path.exists(os.path.join(ndk, "ndk-stack")):
        p = subprocess.run([os.path.join(ndk, "ndk-stack"), "-sym", symdir],
                           input=text, capture_output=True, text=True, errors="replace")
        if p.returncode == 0 and p.stdout.strip():
            body = p.stdout
            print("== symbolicated with %s" % os.path.relpath(symdir, ROOT))
        else:
            print("== ndk-stack failed, raw frames below: %s" % p.stderr.strip()[:160])
    else:
        print("== no unstripped .so found (--symbols DIR); frames stay as addresses")

    keep = []
    for l in body.splitlines():
        s = l.strip()
        if not s or set(s) <= set("*# "):
            continue
        if re.match(r"^(pid:|signal |Abort message|Cause:|backtrace:|#\d+ )", s) or "Build fingerprint" in s:
            keep.append(s)
    print("== native crash (%s)" % os.path.relpath(full, ROOT))
    for l in keep[:a.max]:
        print(l[:300])
    if len(keep) > a.max:
        print("... %d more frames, in the full log" % (len(keep) - a.max))


def parse_ips(path):
    """.ips is a one-line JSON header followed by a JSON payload."""
    with open(path) as fh:
        head = fh.readline()
        rest = fh.read()
    return json.loads(head), json.loads(rest)


def cmd_crash_ios(a):
    reports = os.path.expanduser("~/Library/Logs/DiagnosticReports")
    proc = a.process or IOS_PROC
    files = [f for f in glob.glob(os.path.join(reports, "*.ips"))
             if os.path.basename(f).lower().startswith(proc.lower())]
    if not files:
        files = sorted(glob.glob(os.path.join(reports, "*.ips")), key=os.path.getmtime)[-5:]
        print("== no .ips for %r; newest reports are: %s"
              % (proc, ", ".join(os.path.basename(f) for f in files)))
        return
    path = max(files, key=os.path.getmtime)
    head, body = parse_ips(path)

    print("== %s   %s" % (os.path.basename(path), head.get("timestamp", "")))
    ex = body.get("exception", {})
    print("== %s %s %s" % (ex.get("type", "?"), ex.get("signal", ""), ex.get("subtype", "") or ""))
    for k in ("termination", "asi", "lastExceptionBacktrace"):
        v = body.get(k)
        if k == "termination" and v:
            print("== termination: %s %s" % (v.get("indicator", ""), v.get("reason", ""))[:300])
        elif k == "asi" and v:
            print("== %s" % json.dumps(v)[:300])

    images = body.get("usedImages", [])
    faulting = body.get("faultingThread")
    threads = body.get("threads", [])
    if faulting is None or faulting >= len(threads):
        print("== no faulting thread recorded; full report: %s" % path)
        return

    th = threads[faulting]
    print("== faulting thread %d %s" % (faulting, th.get("name", "") or th.get("queue", "")))
    for i, fr in enumerate(th.get("frames", [])[:a.max]):
        img = images[fr["imageIndex"]] if fr.get("imageIndex", -1) < len(images) else {}
        name = img.get("name", "?")
        sym = fr.get("symbol")
        if sym:
            print("%2d %-22s %s + %d" % (i, name, sym, fr.get("symbolLocation", 0)))
            continue
        # Unsymbolicated: try atos against a matching dSYM/binary by UUID.
        addr = img.get("base", 0) + fr.get("imageOffset", 0)
        resolved = atos(img, addr)
        print("%2d %-22s %s" % (i, name, resolved or ("0x%x +%d" % (addr, fr.get("imageOffset", 0)))))
    print("== full report: %s" % path)


_ATOS_CACHE = {}


def atos(img, addr):
    uuid = (img.get("uuid") or "").upper()
    if not uuid or not shutil.which("atos"):
        return None
    if uuid not in _ATOS_CACHE:
        r = run(["mdfind", "com_apple_xcode_dsym_uuids == %s" % uuid])
        hits = [h for h in r.stdout.splitlines() if h.strip()]
        _ATOS_CACHE[uuid] = hits[0] if hits else None
    dsym = _ATOS_CACHE[uuid]
    if not dsym:
        return None
    bins = glob.glob(os.path.join(dsym, "Contents/Resources/DWARF/*"))
    if not bins:
        return None
    r = run(["atos", "-o", bins[0], "-l", hex(img.get("base", 0)), hex(addr)])
    line = r.stdout.strip()
    return line or None


# ------------------------------------------------------------------------------------ screenshots

def load(path):
    from PIL import Image
    return Image.open(path)


def band_stats(im, bands):
    from PIL import ImageStat
    g = im.convert("L")
    w, h = g.size
    rows = []
    for i in range(bands):
        y0, y1 = h * i // bands, h * (i + 1) // bands
        s = ImageStat.Stat(g.crop((0, y0, w, y1)))
        rows.append((y0, y1, s.mean[0], s.stddev[0]))
    return rows


def cmd_shot(a):
    out = ensure_outdir(a.out)
    path = a.file or os.path.join(out, "shot-%s-%s.png" % (a.platform, stamp()))

    if a.platform == "android":
        dev = pick_android(a.device)
        r = subprocess.run(["adb", "-s", dev, "exec-out", "screencap", "-p"], capture_output=True)
        if r.returncode or not r.stdout:
            sys.exit("devtap: screencap failed: %s" % r.stderr.decode(errors="replace")[:200])
        with open(path, "wb") as fh:
            fh.write(r.stdout)
    else:
        dev = pick_ios(a.device)
        r = run(["xcrun", "simctl", "io", dev, "screenshot", path])
        if r.returncode:
            sys.exit("devtap: simctl screenshot failed: %s" % r.stderr.strip()[:200])

    im = load(path)
    print("== %s  %dx%d  %.0f KB" % (os.path.relpath(path, ROOT), im.width, im.height,
                                     os.path.getsize(path) / 1024.0))

    if a.crop:
        box = tuple(int(v) for v in a.crop.split(","))
        crop = os.path.splitext(path)[0] + "-crop.png"
        im.crop(box).save(crop)
        print("== crop %s -> %s" % (a.crop, os.path.relpath(crop, ROOT)))
        im = load(crop)
        path = crop

    for y0, y1, mean, sd in band_stats(im, a.bands):
        print("band %4d-%4d  mean %6.1f  stddev %5.1f" % (y0, y1, mean, sd))

    if a.compare:
        print()
        diff_report(a.compare, path, a.bands)

    print("== image NOT read. Open it only if a number above moved.")


def diff_report(pa, pb, bands):
    from PIL import ImageChops, ImageStat
    a, b = load(pa).convert("L"), load(pb).convert("L")
    if a.size != b.size:
        print("== size differs: %s vs %s - not comparable" % (a.size, b.size))
        return
    d = ImageChops.difference(a, b)
    mean, mx = ImageStat.Stat(d).mean[0], d.getextrema()[1]
    print("== diff %s vs %s: mean %.2f  max %d" % (os.path.basename(pa), os.path.basename(pb), mean, mx))
    if mx == 0:
        print("== IDENTICAL. Whatever you changed did not reach the frame "
              "(a state change needs TWO drawn frames).")
        return
    bbox = d.point(lambda p: 255 if p > 8 else 0).getbbox()
    print("== changed region bbox %s" % (bbox,))
    w, h = d.size
    npx = w * (h // bands)
    for i in range(bands):
        y0, y1 = h * i // bands, h * (i + 1) // bands
        band = d.crop((0, y0, w, y1))
        changed = sum(c for v, c in enumerate(band.histogram()) if v > 8)
        print("band %4d-%4d  %5.1f%% pixels changed  mean %5.2f"
              % (y0, y1, 100.0 * changed / max(npx, 1), ImageStat.Stat(band).mean[0]))


def cmd_diff(a):
    diff_report(a.a, a.b, a.bands)
    print("== images NOT read. Open one only if a band above moved.")


# ---------------------------------------------------------------------------------------- selftest

LOGCAT_FIXTURE = """\
09-06 17:59:11.001  4131  4160 I massif  : TileLayer: 2 span reference tiles for 3 stranded ends
09-06 17:59:11.002  4131  4160 I massif  : TileLayer: 7 span reference tiles for 9 stranded ends
09-06 17:59:11.003   980  1002 E ActivityManager: Force stopping com.foo
09-06 17:59:11.004  4131  4160 D massif  : verbose probe
09-06 17:59:11.005  4131  4160 E AndroidRuntime: FATAL EXCEPTION: main
09-06 17:59:11.006  4131  4160 E AndroidRuntime: \tat com.massifmaps.Foo.bar(Foo.java:12)
09-06 17:59:11.007  4131  4160 E AndroidRuntime: \tat com.massifmaps.Foo.baz(Foo.java:34)
"""

IOS_FIXTURE = """\
Timestamp               Ty Process[PID:TID]
2026-09-06 18:00:51.123 Df MassifDemo[4131:99] [massif:render] drape cost 21.0 ms
2026-09-06 18:00:51.124 Df MassifDemo[4131:99] [massif:render] drape cost 22.5 ms
2026-09-06 18:00:51.125 Er locationd[311:12] CLLocationManager failed
"""


def cmd_selftest(a):
    fails = []

    def check(name, got, want):
        if got != want:
            fails.append("%s: got %r want %r" % (name, got, want))

    # Android: pid scoping, noise tags, level floor, numeric collapse, trace folding.
    kept, st = filter_lines(LOGCAT_FIXTURE.splitlines(), None, True, "I", {"4131"})
    check("android entries", len(kept), 2)
    check("android dropped other pid", st["pid"], 1)
    check("android dropped below level", st["level"], 1)
    check("android collapsed numbers", kept[0][0], 2)
    check("android folded trace", len(kept[1][5]), 2)
    check("android trace parent", kept[1][3], "FATAL EXCEPTION: main")

    # Android: --grep bypasses every filter, including the pid scope.
    kept, _ = filter_lines(LOGCAT_FIXTURE.splitlines(), re.compile("Force stopping"), True, "I", {"4131"})
    check("android grep bypass", len(kept), 1)

    # iOS: header row skipped, subsystem used as the tag, level mapped, numbers collapsed.
    kept, _ = filter_lines(IOS_FIXTURE.splitlines(), None, True, "D", set(), ios=True)
    check("ios entries", len(kept), 2)
    check("ios tag", kept[0][2], "massif:render")
    check("ios collapsed", kept[0][0], 2)
    check("ios level", kept[1][1], "E")

    # .ips parsing, on a synthetic report with the shape the real ones have.
    import tempfile
    ips = {"exception": {"type": "EXC_CRASH", "signal": "SIGABRT"}, "faultingThread": 1,
           "usedImages": [{"name": "libA", "base": 4096}, {"name": "libB", "base": 8192}],
           "threads": [{"frames": []},
                       {"name": "main", "frames": [{"imageIndex": 1, "imageOffset": 16, "symbol": "boom"}]}]}
    with tempfile.NamedTemporaryFile("w", suffix=".ips", delete=False) as fh:
        fh.write(json.dumps({"timestamp": "now"}) + "\n" + json.dumps(ips))
        p = fh.name
    head, body = parse_ips(p)
    check("ips header", head["timestamp"], "now")
    check("ips faulting thread", body["threads"][body["faultingThread"]]["name"], "main")
    os.unlink(p)

    for f in fails:
        print("FAIL " + f)
    print("selftest: %d checks, %d failed" % (14, len(fails)))
    sys.exit(1 if fails else 0)


# ------------------------------------------------------------------------------------------- main

def main():
    p = argparse.ArgumentParser(prog="devtap", description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--out", help="artifact directory (default build/devtap)")
    sub = p.add_subparsers(dest="cmd", required=True)

    lg = sub.add_parser("logs", help="filtered, deduplicated log tail")
    lg.add_argument("platform", choices=["android", "ios"])
    lg.add_argument("--device", help="adb serial or simulator udid")
    lg.add_argument("--tag", action="append", help="logcat tag filter, repeatable (android)")
    lg.add_argument("--grep", help="regex; keeps only matching lines, skips the noise filter")
    lg.add_argument("--predicate", help="raw os_log predicate (ios)")
    lg.add_argument("--since", help="android: logcat -t arg; ios: log show --last arg (e.g. 3m)")
    lg.add_argument("--lines", type=int, default=2000, help="android raw lines to pull (default 2000)")
    lg.add_argument("--level", default="I", choices=list("VDIWEF"), help="minimum level (default I)")
    lg.add_argument("--max", type=int, default=40, help="unique lines to print (default 40)")
    lg.add_argument("--frames", type=int, default=6, help="stack frames per entry (default 6)")
    lg.add_argument("--package", default=ANDROID_PKG, help="app to scope to (android)")
    lg.add_argument("--all", action="store_true", help="every pid, not just the app (android)")
    lg.add_argument("--no-filter", action="store_true", help="keep noise tags")
    lg.set_defaults(fn=cmd_logs)

    cr = sub.add_parser("crash", help="crashing thread only, symbolicated")
    cr.add_argument("platform", choices=["android", "ios"])
    cr.add_argument("--device", help="adb serial (android)")
    cr.add_argument("--process", help="process name to match .ips against (ios)")
    cr.add_argument("--symbols", help="directory with the unstripped .so (android)")
    cr.add_argument("--max", type=int, default=30, help="frames to print (default 30)")
    cr.set_defaults(fn=lambda a: cmd_crash_android(a) if a.platform == "android" else cmd_crash_ios(a))

    sh = sub.add_parser("shot", help="screenshot to disk + statistics, never the image")
    sh.add_argument("platform", choices=["android", "ios"])
    sh.add_argument("--device", help="adb serial or simulator udid")
    sh.add_argument("--file", help="output png path")
    sh.add_argument("--crop", help="x0,y0,x1,y1 - also writes a cropped png and stats it")
    sh.add_argument("--compare", help="previous png to diff against")
    sh.add_argument("--bands", type=int, default=8, help="horizontal bands (default 8)")
    sh.set_defaults(fn=cmd_shot)

    df = sub.add_parser("diff", help="A/B image comparison as numbers")
    df.add_argument("a")
    df.add_argument("b")
    df.add_argument("--bands", type=int, default=8)
    df.set_defaults(fn=cmd_diff)

    st = sub.add_parser("selftest", help="check the log parsers against fixtures, no device needed")
    st.set_defaults(fn=cmd_selftest)

    a = p.parse_args()
    a.fn(a)


if __name__ == "__main__":
    main()
