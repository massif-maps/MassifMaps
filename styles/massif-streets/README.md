# Massif Streets

The MapLibre style JSON is the **source of truth**. The CartoCSS the SDK reads is generated from it
with `massif-style mapbox2css`, and is never hand-edited; anything CartoCSS can express and MapLibre
cannot goes in a hand-owned overlay beside the generated file.

**Stage: road shields.** Roads, water and landcover are drawn as context so a shield has something
to sit on — they are not the final design.

```sh
node ../../tools/style-sprite/build.mjs sprite-src sprite sprite   # after touching sprite-src/
python3 ../../tools/style-preview/serve.py --mbtiles <name>=<path>.mbtiles
open 'http://127.0.0.1:8787/?style=http://127.0.0.1:8787/styles/massif-streets/style.json'
```

## Shields

`transportation_name.network` picks the artwork. Stock OpenMapTiles fills it from the route
relation, so OpenFreeMap carries `us-interstate`, `us-highway` and `us-state` — that is where the US
artwork is tested, and Denver at z10 shows all three.

A ref with any other network, or none at all, takes `shield-default`. That is not a fallback for its
own sake: our planetiler fork does not emit `network` at all today, so the same style has to stay
readable over both tilesets, and a French `D 1075` gets a plate either way.

One sprite covers every ref length. `icon-text-fit: both` grows the image over the `stretchX`
columns the sprite index declares, so there is no `-1`/`-2`/`-3` artwork to keep in step. `ref_length`
is not needed anywhere — MapLibre has `length`, CartoCSS has `length()`.

`shield-us-state` is a neutral plate. Real state routes are per-state artwork (MUTCD M1-5 is a
blank), so it stays generic until the artwork exists.

## Licensing

`shield-us-interstate` and `shield-us-highway` follow MUTCD M1-1 and M1-4 — US federal works, public
domain. Everything else here is drawn for this project. No MapTiler or Mapbox style is copied.

## Not yet ours

`glyphs` points at OpenFreeMap's font server, because there is no font pipeline here yet. It has to
move before this style ships.
