# Massif Streets

The MapLibre style JSON is the **source of truth**. The CartoCSS the SDK reads is generated from it
with `massif-style mapbox2css --fold-casings`, and is never hand-edited; anything CartoCSS can
express and MapLibre cannot goes in a hand-owned overlay beside the generated file.

**Stage: road shields.** Roads, water and landcover are drawn as context so a shield has something
to sit on — they are not the final design.

```sh
node ../../tools/style-sprite/build.mjs sprite-src sprite sprite   # after touching sprite-src/
python3 ../../tools/style-preview/serve.py --mbtiles <name>=<path>.mbtiles
open 'http://127.0.0.1:8787/?style=http://127.0.0.1:8787/styles/massif-streets/style.json'
```

## Shields: a sprite per colour, picked per feature

Written the ordinary MapLibre way — `icon-image` names a sprite, `icon-text-fit` grows it over the
`stretchX` columns the index declares, and one image covers every ref length so `ref_length` is
needed nowhere. Turning that into the SDK's own plate (`text-background-fill` and
`-border-fill`, drawn with no sprite at all) is `mapbox2css`'s job, not the style's.

`shield-plate.svg` is one drawing and the sprite build writes it once per colour from the
`variants` block in `sprite-src/manifest.json`. `us-interstate` and `us-highway` keep real artwork,
because there the outline *is* the shield.

The colour comes from `iso_a2` and the ref's first letter: French `A`/`N` red, `D`/`M` yellow;
German `A` blue, `B` yellow; British `M` blue, `A` green; Dutch `A` red, `N` yellow; Swiss, Italian
and Spanish by class. `e-road` wins over the country. **`iso_a2` is read through a `coalesce`**, so
a tileset without it takes no country branch and every ref lands on the neutral plate — see
[what the style needs from the tileset](../../docs/contributing/tileset-asks.md).

An SDF plate tinted by `icon-color` was tried and abandoned. `icon-text-fit` needs the stretch
boxes, and repeating a texel column smears the distance field wherever that column still encodes
the distance to a *side* — which on a plate this size is everywhere. It rendered as a bar in the
halo colour at each end.

## Roads are a pair here and one line on the device

MapLibre has no `line-border-color` — it is not in the style spec, only Mapbox GL v3 and our
CartoCSS have it. So the source of truth carries `road-casing` under `road-fill` with the same
filter, zoom range and joins, and `--fold-casings` merges them into one `line-border-*` rule. It
refuses to fold a dashed casing, which is the case that has to stay two layers anyway.

## Licensing

`shield-us-interstate` and `shield-us-highway` follow MUTCD M1-1 and M1-4 — US federal works, public
domain. Everything else here is drawn for this project. No MapTiler or Mapbox style is copied.

## Owed

- `glyphs` points at OpenFreeMap's font server; there is no font pipeline here yet.
- `mapbox2css` draws these as sprites. Folding a per-colour plate onto `text-background-*` — one
  rule with the colour read per feature, no artwork shipped — is converter work not done yet.
- A ref whose letter no country branch names — `VV1` on a French cycleway — takes the neutral
  plate, which on a light background is held together only by its border.
