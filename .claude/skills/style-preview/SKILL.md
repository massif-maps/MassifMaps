---
name: style-preview
description: Look at a map style the way it renders - MapLibre over OpenFreeMap and over a local tileset, the SDK's own web build under both, Mapbox Standard as a reference, day/night, tilt. Use for ANY visual question about styles/massif or a converted CartoCSS style ("does it look right", "compare with mapbox/alpimaps", "why is X missing at zoom N", "check the e-ink/OSM variant", a style release check).
---

# style-preview — the four-pane style bench

`tools/style-preview` is the style dev page: one style drawn by MapLibre over OpenFreeMap (top left)
and over a local `.mbtiles` (top right), and the CartoCSS the converter made of it drawn by the SDK's
web build under each (`massif row`). Full doc: `docs/contributing/style-preview.md`.

## Start it

- Built SDK web module needed in `web/demo/` (`massif-demo.{mjs,wasm,data}`): `python3 scripts/build-web.py --profile lite --configuration RelWithDebInfo --build-demo` (incremental after a C++ change), or the `web-preview` workflow artefact.
- The converted project: `cd styles/massif && python3 build.py --convert` (needs `tools/style-cli` built: `npm ci && npm run build` there).
- Serve: the `style-preview` entry of `.claude/launch.json` (`preview_start`), or
  `python3 tools/style-preview/serve.py --port 8787 --mbtiles NAME=a.mbtiles[+b.mbtiles...]`.
  `+` merges archives per tile as `MergedMBVTTileDataSource` does (bathymap, contours, routes).

## URL parameters

`/?style=http://localhost:PORT/styles/massif/<variant>.json` then:

| Param | Does |
|---|---|
| `massif=1` | open with the SDK row |
| `lon`, `lat`, `zoom`, `tilt`, `rot` | camera (MapLibre conventions) |
| `hour=22` | SDK day-cycle light; the reference takes the matching preset |
| `ref=mapbox-standard` | Mapbox Standard in the top-right slot (needs `~/.mapbox_token`) |
| `project=osm\|custom` | an extension example instead of the variant |
| `compare=/styles/<dir>&compareVariant=<name>` | another CartoCSS project in the left SDK pane, local tiles |
| `shadows=1` | building shadows (slow) |
| `blendspeed=0`, `labelperspective=0`, `dpi=` | SDK pane knobs |

## Reading it

- Prefer `read_page` / `javascript_tool` over screenshots. The panes are iframes `massif-l`, `massif-r`; each exposes `massif`, `camera`, `module`. Move one: `frame.contentWindow.massif.call(frame.contentWindow.camera.handle, 'moveTo', [[lon,lat], zoom, 0, 90 - tilt, 0])`. Sync the row from MapLibre: `maps[0].jumpTo({...}); postCamera()`.
- A pane's SDK errors show in its `#err` box; MapLibre style errors in the pane's red box.
- After a converter/style change: rebuild (`build.py --convert`) and reload the frame (`frame.src = frame.src`). After an SDK C++ change: rebuild the web module first.
- A release build (`styles/massif/release/release.py`): `/styles/massif/demo/?from=dist|npm|site&version=<v>` loads its MapLibre flavour, and links its CartoCSS flavours into `?compare=`.
- The MapLibre styles must pass `gl-style-validate` (`@maplibre/maplibre-gl-style-spec`); the release workflow runs it.
