---
title: Flattening the libs submodules
description: Why libs-external and libs-massif stop being submodules, how the history is grafted without rewriting a SHA, and what has to be fixed by hand afterwards.
---

# Flattening the libs submodules

`libs-external/` and `libs-massif/` were submodules of their own forked repos. This page is the
procedure that moves their content into this repo while keeping every **third-party fork** a
submodule at its current path — the same shape `integrations/nativescript` already has.

Run by [`scripts/flatten-submodules.sh`](https://github.com/massif-maps/MassifMaps/blob/master/scripts/flatten-submodules.sh).
Dry run by default; `--apply` executes.

```sh
./scripts/flatten-submodules.sh --phase external          # prints what it would do
./scripts/flatten-submodules.sh --phase external --apply
```

## Why

Both wrappers are forks of **archived** CartoDB repos, so there is no upstream left to merge from.
Measured on 2026-08-22:

| | `libs-massif` | `libs-external` |
|---|---|---|
| tracked content | 3.3 MB, 8 dirs, pure C++ | 30.3 MB / 1196 files + 21 nested submodules |
| pack | 1.78 MiB | 146 MiB |
| commits total / last 6 months | 635 / **142** | 341 / 26 |
| this repo, same window | 408 | 408 |
| own CI, releases, other consumers | none, none, none | none, none, none |

`libs-massif` runs at roughly one commit for every three here — it is co-developed, not a
dependency. Every change to it cost a second branch, a second PR with a merge ordering, and a
pointer-bump commit whose title the changelog generator then quotes verbatim (v6.0.0's release
notes had to be rewritten by hand for exactly this).

The pin that submodules buy was not being collected either: CI fetches with
`git submodule update --init --remote --recursive`, and `--remote` takes the tip of the tracked
branch, ignoring the recorded SHA.

`libs-external` is mostly **massif-written build glue** — per-directory `CMakeLists.txt` plus the
header-only vendored trees (`cglib`, `stdext`, `picojson`, `pbf`, `tinyformat`, `utf8`, `tess2`,
`msdfgen`, `nanosvg`, `asio`, `botan`, `pion`, `sqlite`, `rg_etc1`, `pvrt`, `bidi`,
`androidcpufeatures`). The actual third-party source stays in its own repos as submodules, so the
license boundary is unchanged.

## How the history is grafted

Apple Git ships no `git-subtree`, and `git filter-repo` would rewrite every SHA in this repo —
a force-push and a re-clone for everyone. The script uses the recipe `git subtree add` is built
on, which adds an ordinary merge commit and **rewrites nothing**:

```sh
git merge -s ours --no-commit --allow-unrelated-histories <sha>
git read-tree --prefix=libs-external/ -u <sha>
git commit
```

`read-tree --prefix` refuses a path already in the index, so unregistering the submodule has to be
its own commit first. Three commits result per phase: unregister, graft, re-register the nested
forks.

The graft pins the submodule's **current HEAD**, and the script refuses unless that commit is an
ancestor of `origin/develop` — otherwise the graft would reference a commit nobody else has.

## Hoisting the nested submodules

`git` only reads the **root** `.gitmodules`. The script copies every entry out of
`libs-external/.gitmodules` into it, prefixing the name and the path and copying the remaining keys
verbatim, so fork branches (`freetype` @ `fast-sdf`, `valhalla` @ `mbtiles-support`, `protobuf` @
`3.20.x`, …) and `mlt`'s `shallow = true` survive:

```ini
[submodule "libs-external/freetype/freetype"]
	branch = fast-sdf
	path = libs-external/freetype/freetype
	url = https://github.com/massif-maps/freetype.git
```

Three vendored trees are permanently dirty in the working tree (`brotli/brotli`, `date/date`,
`valhalla/valhalla` — untracked build stubs and nested pointers we never touch). Inside a submodule
that noise was contained; hoisted, it would sit next to SDK edits in `git status` and get staged by
accident, so those three get `ignore = dirty`. That also hides genuine local edits to them, which
is acceptable for trees we only ever bump.

## What the script does not do

- **Re-clone cost.** `.git/modules/libs-external` is deleted, so all 21 forks are cloned again —
  `valhalla` and `mlt` dominate. The script re-applies `mlt`'s sparse checkout
  (`git -C libs-external/mlt/mlt sparse-checkout set cpp`, without which the 142 MB of test
  fixtures come down) and restores the gitignored `libs-external/boost` symlink it had to delete
  with the directory.
- **Branches.** 17 remote branches in `libs-external`, 20+ in `libs-massif` are not migrated.
  Merge or re-apply them by hand; do the flattening in a window where they are drained.
- **Prose and CI**, which still describe the old layout:

| File | What changes |
|---|---|
| `BUILDING.md` (~22-35) | drop the `cd libs-external` step; keep the `mlt` sparse-checkout |
| `.github/workflows/build.yml` (242, 469) | `--remote` no longer moves the flattened trees |
| `CLAUDE.md` | repository layout table, "Submodule gotcha" paragraph |
| `.claude/CLAUDE.md` | "Repos — one fork, two submodules, three PR targets" |

`scripts/build/CMakeLists.txt:18` needs **no** change: it resolves
`SDK_EXTERNAL_LIBS_DIR` by path, and the path does not move.

## Every other checkout

```sh
git submodule deinit -f -- libs-external libs-massif
git submodule update --init --recursive
```

## Known gaps

- The graft keeps history but not path-following: `git log` on a file grafted from `libs-massif`
  stops at the merge unless you pass `--follow`. `filter-repo` would fix that at the cost of
  rewriting every SHA — not worth it.
- The two phases are independent. `libs-massif` is the larger win (142 commits per 6 months against
  26) and the simpler one, having no nested submodules at all; `libs-external` can stay a submodule
  indefinitely without much cost.
