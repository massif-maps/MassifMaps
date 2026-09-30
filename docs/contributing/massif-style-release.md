---
title: Massif style release
description: "Building styles/massif from a clean checkout, releasing it from a machine or from CI, testing a release, and how the website serves it."
sidebar_position: 6
---

# Massif style release

`styles/massif` is Python that writes MapLibre styles; everything a user downloads is generated
from it. Users' side: [Massif](../styles/massif.mdx). How the style itself is written:
[`styles/massif/README.md`](https://github.com/massif-maps/MassifMaps/blob/master/styles/massif/README.md).

## What is where

| Path | What |
|---|---|
| `styles/massif/*.py`, `layers/`, `palette.py` | the generator |
| `styles/massif/sprite-src/`, `sprite/` | the icon SVGs and the sheet made from them (committed) |
| `styles/massif/<variant>.json` | the MapLibre variants (generated, committed) |
| `styles/massif/examples/` | the extension examples (`osm`, `custom`) |
| `styles/massif/release/` | `release.py` (every flavour, npm, GitHub), `site.py` (the website's copy), `screenshots.mjs`, the npm README |
| `styles/massif/demo/` | a page loading the MapLibre styles from the website, npm, a local release or the repo |
| `styles/massif/LICENSE` | MIT-0: use it for anything, no notice required |
| `carto/`, `dist/`, `family.json` | generated, gitignored |
| `tools/style-cli/` | the converter (`mapbox2css`) and compiler (`css2xml`) |
| `tools/style-sprite/` | the sprite sheet (`build.mjs`) and the icon font (`iconfont.mjs`) |
| `tools/style-preview/` | the style dev page ([style preview](style-preview.md)) |
| `.github/workflows/release-styles.yml` | CI build of every flavour; the published release |

## Building, from a clean checkout

```sh
# the tools
(cd tools/style-cli && npm ci && npm run build)
(cd tools/style-sprite && npm ci)
(cd styles/massif/release && npm ci && npx playwright install chromium)   # screenshots only
# the style compiler as wasm, for the compiled flavours: from an SDK v* release, or
(cd libs-massif/cartocss/util && emcmake cmake -B build-wasm -DCMAKE_BUILD_TYPE=MinSizeRel \
   && emmake make -C build-wasm massif-style && cp build-wasm/massif-style.{mjs,wasm} ../../../tools/style-cli/wasm/)

cd styles/massif
node ../../tools/style-sprite/build.mjs sprite-src sprite sprite   # after an SVG change
python3 build.py              # the MapLibre variants
python3 build.py --convert    # and the SDK project into carto/, for the style preview
```

## The flavours

`python3 release/release.py <version> [--only FLAVOUR]... [--screenshots]` writes `dist/`: one folder
and one `massif-<flavour>-<version>.zip` per flavour, `dist/npm/` (the npm package), and with
`--screenshots` one JPEG per variant.

- **`maplibre`** — the committed variants, with the sources that have no public host (`bathymap`,
  `contours`, `routes`, `satellite`) swapped for **empty placeholders of the same name**. MapLibre
  refuses a style whose layer names a missing source, so the layers stay and the source is empty
  GeoJSON — or, for the raster, one transparent `data:` tile at z0: a raster source with no URL throws,
  and a raster layer reading GeoJSON stops the whole map drawing. `sprite` is made absolute, the
  website's `v<version>/` folder by default (`--base-url` to change it): MapLibre resolves a relative
  sprite against the page, not the style, and **draws nothing at all when the sprite fails**.
  Minified; `metadata["massif:version"]` and `["massif:placeholder-sources"]` added.
- **`cartocss`** — `build.convert()` into the flavour folder, as `--convert` does into `carto/`.
- **`cartocss-iconfont`** — the same conversion with `--icon-font MassifIcons --icon-font-map
  MassifIcons.json --icon-font-size 27 --fonts`. `tools/style-sprite/iconfont.mjs` builds the font
  from `sprite-src/poi/`: each badge SVG loses its plate (the first `<circle>`), its glyph box
  (`10.5 10.5 27 27` of the 48 px badge) becomes the em, codepoints run from U+E001 in name order,
  and every name is mapped twice (`restaurant`, `restaurant-poi`). 27 is that box's height, so
  `icon-size` scales a glyph exactly as it scaled the sprite; the converter still measures the
  sheet's disc once for the plate the SDK draws under the glyph. A codepoint is not a contract —
  adding an icon moves the ones after it — the map file is.
- **`*-compiled`** — `massif-style css2xml` of every `<name>.json` of the folder, beside its
  `icons/`, `fonts/` and `legend.json`. The XML keeps the parameters live (`[param::variant]`).

## Checking a build

- `gl-style-validate` (`@maplibre/maplibre-gl-style-spec`) over `dist/maplibre/*.json` — CI runs it.
  It is what caught `road_osm_low`'s `["config", …]` reaching MapLibre paint: MapLibre has no
  `config` expression and the whole style failed to load. `build.maplibre_style` folds every
  `config` to the variant's default (`lib.fold_config`), keeping it only in `massif:*` metadata.
- `build.convert` fails on any `when()` in the CartoCSS (per-feature, blocks rule pruning); the
  compiled flavours are the real compiler's output, so a CartoCSS that does not compile fails.
- **Look at it**: run the [style preview](style-preview.md) server, then
  - `/styles/massif/demo/?from=dist` — the MapLibre flavour of `dist/`, variant by variant; its
    **SDK** links open the style preview with the build's CartoCSS and icon-font flavours in the
    compare pane (`?compare=/styles/massif/dist/cartocss-iconfont`);
  - `?from=npm&version=<v>` — the same for a published package, through unpkg;
  - `?from=site` / `site-v` — the website's copy, once deployed.

## Releasing from a machine

To try the npm package before an official release — publish a prerelease under the `next`
dist-tag, which `npm install @massif-maps/styles` does not pick up:

```sh
npm login                                   # once; an account with publish rights on @massif-maps
cd styles/massif
python3 release/release.py 1.0.0-rc.1 --screenshots --npm next --dry-run   # everything but the upload
python3 release/release.py 1.0.0-rc.1 --screenshots --npm next             # publish the prerelease
python3 release/release.py 1.0.0-rc.1 --screenshots --npm next --github    # and a GitHub prerelease
```

Then `demo/?from=npm&version=1.0.0-rc.1` draws it from unpkg. `--github` makes the
`massif-styles-v<version>` release with every zip (`gh`, logged in; a version with `-` is marked a
prerelease), and the website picks it up on its next deploy. A prerelease's MapLibre JSON names the
website's `v<version>/` sprite, which exists only once the site has deployed that release: until
then load it with the demo's **sprite beside the JSON**.

An official release from a machine is the same with `--npm latest --github`; prefer CI, which adds
npm provenance.

## Releasing from CI

[`release-styles.yml`](https://github.com/massif-maps/MassifMaps/blob/master/.github/workflows/release-styles.yml):

1. **On a pull request** touching `styles/massif`, `tools/style-sprite` or the converter: builds the
   wasm, every flavour and the screenshots, validates, uploads it all as an artefact. Nothing is
   published.
2. **Actions → Release Massif styles → Run workflow**, `version` (no `v`) and `publish` on:
   - npm `@massif-maps/styles@<version>` (`dist/npm`, provenance, trusted publishing — no token);
   - GitHub release `massif-styles-v<version>` with every zip and `massif-screenshots-<version>.zip`;
   - `docs.yml` dispatched, since a release made with `GITHUB_TOKEN` triggers no workflow.

Versions are the style's own semver: a change that moves what a variant draws is a minor, a renamed
or removed parameter or variant a major.

## The website

`docs.yml` runs `styles/massif/release/site.py website/static/styles/massif`: every non-draft
`massif-styles-v*` release's MapLibre zip and screenshots unpacked under `v<version>/`, the newest
also copied to the root (so `…/styles/massif/streets.json` is the latest), and `versions.json`.
With no release yet it builds the root from the checkout.

The doc page's live map and screenshot grid (`website/src/components/MassifStyleMap.js`) read those
paths, load the sprite from the site's own copy, and draw hybrid over EOX's
[Sentinel-2 cloudless](https://s2maps.eu) 2016 mosaic (CC BY 4.0, credited in the map), as the
screenshots do.
