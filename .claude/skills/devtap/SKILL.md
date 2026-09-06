---
name: devtap
description: Read Android/iOS logs, crash reports or device frames. Use INSTEAD of raw `adb logcat`, `adb screencap`, `xcrun simctl log`, reading a `.ips` file, or opening a screenshot — those dump 30k-450k tokens into context, devtap returns 200. Triggers on logcat, crash, tombstone, stack trace, screenshot, screencap, emulator, simulator, "what does the log say", "did it crash", "does it look right".
---

# devtap — device output without the context bill

`scripts/devtap.py`. Writes the full artifact to `build/devtap/` and prints only a summary.
**Never** run the raw command yourself; the whole point is that the bulk never enters context.

Measured on this repo: logcat 2087 lines → 10; iOS `log show` 15193 lines → 8; a 112 KB `.ips` → 17.

## Logs

```bash
python3 scripts/devtap.py logs android --max 20
python3 scripts/devtap.py logs ios --since 3m
```

Android scopes to `com.massifmaps.MassifDemo`'s pid by default, drops ~30 system noise tags,
collapses lines that differ only in numbers/addresses into one `xN` entry, folds a Java stack
trace into its parent line, and sorts errors first so `--max` never truncates the thing that
matters. Header names everything it dropped, so a surprising count is itself a signal.

- `--grep REGEX` — bypass every filter, keep matching lines only. Use for a specific probe.
- `--tag T` — logcat tag filter (android). `--predicate P` — raw os_log predicate (ios).
- `--all` — every pid, not just the app. `--level W` — raise the floor. `--no-filter` — keep noise.
- `--frames N` — stack frames per entry (default 6).

**Two emulators are shared with other sessions** — always pass `--device` or set `ANDROID_SERIAL`;
devtap refuses to guess when several are attached.

Never read the file in `build/devtap/`. `rtk grep` it if you need something the summary dropped.

## Crashes

```bash
python3 scripts/devtap.py crash android            # crash buffer, else last 4000 lines
python3 scripts/devtap.py crash ios --process MassifDemo
```

Android picks the Java `FATAL EXCEPTION` block (own frames first) or the native tombstone, and
symbolicates the tombstone with `ndk-stack` against the newest unstripped `.so` under
`scripts/android-dev/massif/build/intermediates/cxx/` — override with `--symbols DIR`.
iOS parses the newest matching `.ips`, prints exception type / termination / faulting thread only,
and resolves addresses with `atos` when `mdfind` locates a dSYM by UUID.

## Frames

```bash
python3 scripts/devtap.py shot android --compare before.png
python3 scripts/devtap.py diff before.png after.png
```

`shot` captures, then prints per-band mean/stddev — **not the image**. `--crop x0,y0,x1,y1` writes
and stats a region. `diff` prints mean/max, the changed-region bbox and the % of pixels changed per
horizontal band.

**Do not open the png unless a number moved.** `mean 0.00 max 0` means the frames are identical —
that is the answer, and it usually means a state change never reached the frame (the surface is
double-buffered: a change needs TWO drawn frames). A band at 0.0% means that content is not drawn
there at all, which is what separates "tile never loaded" from "tile drawn but depth-rejected".

A frame taken before the scene settles (60–90 s) is not evidence — see
[demo-app.md](../../../docs/contributing/demo-app.md).

## After touching devtap.py

`python3 scripts/devtap.py selftest` — 14 checks on the log parsers and `.ips` reader against
fixtures, no device needed. Run it before committing a change to the regexes.
