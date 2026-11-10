#!/usr/bin/env python3
"""Long-frame statistics from a -PprofileRender log: the frame-to-frame interval, not the per-second average.

    adb logcat -d -s massif | python3 scripts/frame-hitch-stats.py [--merge]

Reads the 'PROF RAW' lines (FrameProfiler::recordInterval): per frame interval:work:sky:bakes:tileSets:labels.
Prints the frames over 50 / 100 ms, p50 / p95 / p99 / max per 10 s of rendering, and how often a frame is long
by the drape bakes it did. An interval over 1 s is an idle gap and ends a run; the longest run is the pan,
--merge joins them all (short bursts, e.g. a touch pan).
"""
import re
import sys


def percentile(values, p):
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, int(len(ordered) * p / 100))]


def report(intervals, tag):
    if not intervals:
        print(tag, 'no data')
        return
    print(f"{tag}: n={len(intervals)} dur={sum(intervals) / 1000:.1f}s >50={sum(v > 50 for v in intervals)} "
          f">100={sum(v > 100 for v in intervals)} p50={percentile(intervals, 50):.0f} p95={percentile(intervals, 95):.0f} "
          f"p99={percentile(intervals, 99):.0f} max={max(intervals):.0f}")


def main():
    runs = [[]]
    for line in sys.stdin:
        match = re.search(r'PROF RAW:(.*)', line)
        if not match:
            continue
        for token in match.group(1).split():
            frame = [float(x) for x in token.split(':')]
            if len(frame) == 6:
                if frame[0] >= 1000:
                    runs.append([])
                else:
                    runs[-1].append(frame)
    if '--merge' in sys.argv:
        runs = [[frame for run in runs for frame in run]]
    runs = [run for run in runs if len(run) > 50]
    if not runs:
        print('no PROF RAW run')
        return
    frames = max(runs, key=lambda run: sum(frame[0] for frame in run))
    report([frame[0] for frame in frames], 'all')
    window, elapsed, index = [], 0.0, 0
    for frame in frames:
        window.append(frame[0])
        elapsed += frame[0]
        if elapsed >= 10000:
            report(window, f'  10 s window {index}')
            window, elapsed, index = [], 0.0, index + 1
    for name, picked in (('with a bake', [f for f in frames if f[3] >= 1]), ('without', [f for f in frames if f[3] == 0])):
        if picked:
            print(f"  {name}: {len(picked)} frames, {100 * sum(f[0] > 50 for f in picked) / len(picked):.0f}% over 50 ms, "
                  f"mean interval {sum(f[0] for f in picked) / len(picked):.1f} ms")


main()
