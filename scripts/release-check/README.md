# Release check

The npm packages consumed the way an app does — from npm or from `pack`'s tarballs, never from this
repo's sources. Part of the pre-release steps in [BUILDING.md](../../BUILDING.md#pre-release-then-release).

```sh
cd scripts/release-check
npm install                                   # the `next` dist-tag from npm
npm install --no-save ../../dist/npm/*.tgz    # or: the tarballs `npm-packages.py pack` wrote
npm run check     # api imports, web ships both modules, style-tools compiles and converts the styles
npm run serve     # http://localhost:8099: a map from @massif-maps/web drawn with @massif-maps/styles
```

The page reports on itself (top left). Try `?variant=full` — offline routing must then be present —
and `?style=outdoor`, `topo`, `hybrid`, `eink`. For a final release, `npm install
@massif-maps/web@latest ...` instead.
