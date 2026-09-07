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
./scripts/flatten-submodules.sh --phase massif          # prints what it would do
./scripts/flatten-submodules.sh --phase massif --apply
```

**Status:** `libs-massif` is flattened (2026-08-23). `libs-external` is still a submodule and can
stay one — see [Known gaps](#known-gaps).

Do `libs-massif` first: it is the bigger win and has no nested submodules of its own.

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

The pin that submodules buy went uncollected for a long time: CI fetched with
`git submodule update --init --remote --recursive`, and `--remote` takes the tip of the tracked
branch, ignoring the recorded SHA. That is fixed — the fetch steps are pinned now — but it means
the pointers only started being honoured recently.

`libs-external` is mostly **massif-written build glue** — per-directory `CMakeLists.txt` plus the
header-only vendored trees (`cglib`, `stdext`, `picojson`, `pbf`, `tinyformat`, `utf8`, `tess2`,
`msdfgen`, `nanosvg`, `asio`, `botan`, `pion`, `sqlite`, `rg_etc1`, `pvrt`, `bidi`,
`androidcpufeatures`). The actual third-party source stays in its own repos as submodules, so the
license boundary is unchanged.

## How the history comes across

Full history, and `git log` keeps working on every imported path. Those are not in tension, which
is not obvious — the naive reading is that preserving `git log` costs a history rewrite of *this*
repo. It does not. `git filter-repo` runs on a **throwaway clone of the submodule**:

```sh
git clone --no-local libs-massif "$scratch/src"        # while the directory still exists
git -C "$scratch/src" checkout -B flatten-src <sha>    # filter-repo needs a branch, not a detached HEAD
git -C "$scratch/src" filter-repo --force --to-subdirectory-filter libs-massif
# ... unregister the submodule, then:
git merge --allow-unrelated-histories flatten-libs-massif/flatten-src
```

Because the clone's paths already carry the `libs-massif/` prefix, the last step is an ordinary
merge — no `read-tree --prefix`, and **no SHA in this repo changes**. The imported commits get new
SHAs, so an old reference to a `libs-massif` commit only resolves in the fork, which stays online.

Apple Git ships no `git-subtree`, so the alternative was its underlying recipe
(`merge -s ours` + `read-tree --prefix`). Measured on synthetic repos, that variant leaves the
historical paths at the root and the log stops dead at the merge:

| | `git log -- libs-massif/vt/…` | `--follow` | `git blame` |
|---|---|---|---|
| `merge -s ours` + `read-tree` | 1 commit (the merge) | 0 | follows |
| filter-repo re-prefix, then merge | **full history** | n/a | follows |

Unregistering the submodule still has to be its own commit — the path must be free before the merge
writes files into it, and `prepare_history` must clone **before** `drop_submodule` deletes the
directory. Three commits result per phase: unregister, import, re-register the nested forks.

The import pins the submodule's **current HEAD**, and the script refuses unless that commit is an
ancestor of `origin/develop` — otherwise the import would reference a commit nobody else has.

### Measured, on a throwaway clone of the SDK

`--phase massif --apply`, run end to end against a full clone:

- `master` unchanged — the pre-existing 3483 commits keep their SHAs
- 677 commits and all 8 tags imported; total goes 3484 → 4163 (+677 +2 for unregister and merge)
- `git rev-parse HEAD:libs-massif` equals the submodule's tree at that SHA — **identical hash**,
  292 files either side
- `git log -- libs-massif/vt/src/vt/GLTileRenderer.cpp` → **198 commits**; blame still reaches
  `mtehver`
- working tree populated, `git status` clean

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
- **Prose**, which still describes the old layout:

| File | What changes |
|---|---|
| `CLAUDE.md` | repository layout table, the repos/PR-targets table, "Library documentation" |
| `BUILDING.md` | external phase only — drop the `cd libs-external` step, keep the `mlt` sparse-checkout |

**No CI or build edit is needed**, because nothing outside the prose treats these trees as *repos*
— only as paths, and the paths do not move:

- `scripts/build/CMakeLists.txt:18` resolves `SDK_EXTERNAL_LIBS_DIR` by path
- `.github/workflows/build.yml` fetches with `git submodule update --init --recursive`, which
  initializes whatever is still a submodule, and reaches the flattened tree by path
  (`cd libs-massif/cartocss/util`)
- `BUILDING.md:139` is a path too

## Every other checkout

```sh
git submodule deinit -f -- libs-external libs-massif
git submodule update --init --recursive
```

## Known gaps

- Imported commits get new SHAs. A `libs-massif` SHA quoted in an old PR description, doc or commit
  message resolves only in the fork, not here. The fork stays online, so nothing is lost — but the
  two histories are not SHA-comparable after this.
- `git filter-repo` is required (`brew install git-filter-repo`); the preflight refuses without it
  rather than falling back to the variant that breaks `git log`.
- The two phases are independent. `libs-massif` is the larger win (142 commits per 6 months against
  26) and the simpler one, having no nested submodules at all; `libs-external` can stay a submodule
  indefinitely without much cost.
