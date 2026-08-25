---
title: Maintenance
description: Procedures for keeping vendored dependencies and generated artefacts up to date.
---

# Maintenance docs

Procedures for keeping the vendored dependencies and the generated artefacts of this fork up to
date. Same rule as [`docs/internals/rendering/`](../internals/rendering/index.mdx): a procedure that had to be
rediscovered gets written down here in the SAME commit as the work — exact commands, the versions
they were run with, what breaks when a step is skipped, and the dead ends.

| Page | What it covers |
|------|----------------|
| [`valhalla-upgrade.md`](valhalla-upgrade.md) | Merging a new upstream Valhalla release into `mbtiles-support`, regenerating protos / locales / the tz database, and the fork patches that must survive |
| [`mac-catalyst.md`](mac-catalyst.md) | Why the Catalyst slices are a macOS project in disguise, what that breaks at link time, and what the build gives up to work around it |
| [`angle.md`](angle.md) | Rebuilding the vendored MetalANGLE static slices, the strip settings that make them shippable, and the one fork patch to re-apply |
| [`flatten-submodules.md`](flatten-submodules.md) | Moving `libs-external` / `libs-massif` in-tree while keeping the third-party forks as submodules — the history graft, the `.gitmodules` hoist, and what has to be fixed by hand |
| [`api-typings-chain.md`](api-typings-chain.md) | From a `.i` declaration to an app's autocompletion: what generates what, the `--defines` trap, and how to tell which link is stale |
| [`branding.md`](branding.md) | The one mark every logo, favicon, social card and launcher icon is generated from — and why the Android safe zone is smaller than it looks |
