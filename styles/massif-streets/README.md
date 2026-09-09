# Massif Streets

The MapLibre style JSON is the **source of truth**. The CartoCSS the SDK reads is generated from it
with `massif-style mapbox2css --fold-casings --tile-draw-size 512 --fonts fonts`, and is never hand-edited; anything CartoCSS can
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

## A road's casing is an outline beside it, not a wider line under it

`road-casing` used to be exactly that — a wider line under `road-fill`, same filter, same zoom
range — so that `--fold-casings` could merge the pair into one `line-border-*` rule, which is what
MapLibre has no way to state (`line-border-color` is Mapbox GL v3 and our CartoCSS, not the spec).

It is Mapbox Standard's shape now: a `line-gap-width` outline drawn OUTSIDE the fill, a near-hairline
of 1 px at z14 reaching 2 by z22. That came with Standard's road widths and could not be separated
from them — see "Taken from Mapbox Standard" below for why taking the widths alone looks wrong.

**It does not stop at z15, where Standard stops it.** Standard's gate is there because a casing on a
half-pixel road is all casing, but the fill ramp already fades a class in by WIDTH, so the casing can
mirror its zero points and follow it all the way down: 0.5 px for motorway/trunk/primary at z3,
nothing for the rest until they widen, everything cased by z12. Zoomed out that is what makes a road
read as one line rather than a coloured thread — Liberty draws its casings from z5 for exactly that
reason, and cutting them at z15 was the most visible thing lost when Standard's widths came in.

**`--fold-casings` is a no-op for this style.** There is no casing/fill pair left to fold. It was
already doing nothing before the change — the fold refuses a pair whose fill states a
`line-sort-key`, and `road-fill` states one to order the classes — so the flag has been inert for a
while. It stays on the command line because it costs nothing and would apply again if a foldable
pair were added.

## Taken from Mapbox Standard

Standard is the better-drawn of the two references for road furniture, so its numbers are worth
lifting rather than inventing. Its zoom stops are on the 512 px tile, which is the convention this
style is authored in, so they transfer as written.

**Rail and trams.** `road-rail` + `road-rail-tracks`, the two-layer idiom: a `line-gap-width` pair
for the rails themselves (gap 0 at z15 opening to 20 at z22, each rail 0.5 → 2 px), and over it a
wide line worn almost entirely away by a very short dash — 2 → 32 px carrying `[0.05, 0.5]` — which
is what draws the sleepers. Standard covers `major_rail` and `minor_rail` in one layer;
OpenMapTiles splits them into `class = 'rail'` and `class = 'transit'` (trams, subways, light rail),
so this is **two pairs with an `==` each** rather than one pair over an `in`. That keeps both
bracketing, and lets a tram be tuned thinner later without touching heavy rail — today they are
identical, which is what Standard does.

The **rails draw wherever the tiles carry them**; only the sleepers wait, for z13 and their own
opacity fade. A hairline rail at low zoom is the line being on the map at all, which is the point.

**Road widths.** Standard's `roads` ramp, mapped onto OMT class names — its `street`/`street_limited`
is our `minor`, and everything it does not name (our `service`) takes its fallback. Ours used to be
narrower than Standard's everywhere except motorway, with the hierarchy compressed differently:

| class | Standard @z14 | ours, before |
|---|---|---|
| motorway | 6.42 | 6.5 |
| primary | 6.01 | 4.8 |
| secondary | 5.06 | 4.0 |
| tertiary | 5.06 | 3.2 |
| minor / street | 2.85 | 2.0 |

The casing came with it, because the two do not separate. Standard draws a near-hairline **outside**
the fill — a `line-gap-width` outline of 1 px at z14 growing only to 2 by z22. Ours was a wider line
*under* the fill. Taking the widths without the casing is what looks wrong: at z12 Standard's minor
road is half a pixel, and the old casing would have drawn 2.5 px of outline around it, so the street
would have read as a grey line with a white thread in it. Standard's own z15 gate is the one number
here we did not take — see the casing section above.

**Road names.** Standard's `road-label`: uppercase at `text-letter-spacing` 0.15, sized 9 → 16 px
over z10 → z18 for the classes down to tertiary, 8 → 14 for `minor`, 6.5 → 13 for `service`, in
`hsl(0, 0%, 25%)` on a `hsl(0, 0%, 95%)` halo. Standard turns the classes on with a `step` over zoom
INSIDE its filter, which is the one thing not taken: a filter that reads the zoom is a `when()`. The
same gate is **one layer per class**, ordered least important first so the motorway's name is placed
first and wins the collision.

## Where this style departs from the references

The preview compares against OpenFreeMap Liberty and Mapbox Standard, and most of what differs
between them and us is a bug on our side. This list is the opposite: the places where we have
looked at what they do and **decided against it**. Add to it rather than quietly re-converging.

### A POI wins a collision against a road shield

Both references give it to the shield. Symbol placement runs in REVERSE layer order — maplibre's
`pauseable_placement.ts` walks the style's layers from the last to the first, and whoever is placed
first claims the slot — so Liberty's shields (layers 98–100) beat its POIs (91–94). The SDK does the
same by the opposite arithmetic: the converter numbers `text-placement-priority` up with the layer
index and `LabelCuller` sorts it down, so the later layer still wins.

We want the POI. A shield repeats along its road and can be read a hundred metres further on; a POI
is one place and is either drawn or lost. So **the POI layers go BELOW the shield layers** in
`style.json` — later in the array is a higher index, a higher priority, and placement first.

Not implemented: this style has no POI layers yet. It is written down here because the ordering is
invisible in the output — nothing in the generated CartoCSS says "this was deliberate" — and the
natural thing to do when adding POIs is to copy Liberty's order and inherit its answer.

## Written so the CartoCSS brackets

A CartoCSS `when(...)` is evaluated per feature and the compiler cannot prune around it, so this
style is written to give the converter tests it can bracket — see
[style-tools](../../docs/contributing/style-tools.md), "Bracketed predicates, not `when()`":

- **No `coalesce` null guard in a filter.** A missing field already compares unequal, in MapLibre
  and in mapnikvt alike. The guard only stopped the test bracketing. In a VALUE it still earns its
  place, where `slice` and `upcase` need a string.
- **One layer per zoom band, not a zoom test inside a filter.** The road shields come in by class
  over three layers — motorway and trunk at 9, primary at 11, the rest at 13 — each excluding what
  an earlier band drew. `minzoom` is a predicate the compiler decides per tile; an `any` of
  zoom-and-class branches is a `when()` that every feature pays at every zoom.

**The generated stylesheet has no `when()` at all.** The last one was a class list on a layer that
paints one way throughout, which `expandSetFilter` used to leave whole; it now splits a set that is
the whole filter regardless, since each attachment is then one bracketed test and nothing else.

## Licensing

`shield-us-interstate` and `shield-us-highway` follow MUTCD M1-1 and M1-4 — US federal works, public
domain. Everything else here is drawn for this project. No MapTiler or Mapbox style is copied.

## Owed

- `glyphs` points at OpenFreeMap's font server, which is what MapLibre reads. The SDK side no
  longer needs it: `fonts/NotoSans-Bold.ttf` ships with the style and `--fonts fonts` wires it
  through `project.json`.
- The country's colour needs `iso_a2` on `transportation_name`, and neither tileset carries it, so
  every plate is still drawn neutral in the preview — see
  [what the style needs from the tileset](../../docs/contributing/tileset-asks.md). The branches
  themselves convert.

What the converted CartoCSS loses, seen side by side in [the preview](../../docs/contributing/style-preview.md):

- **A US shield does not stretch to its ref.** `icon-text-fit` has no CartoCSS equivalent, and
  `shield-us-interstate` / `shield-us-highway` are real artwork rather than generated plates, so a
  long ref overruns the sprite instead of widening it. The generated plates are unaffected: they
  are drawn from `text-background-*`, and `icon-text-fit-padding`'s `[1, 3, 1, 3]` arrives as
  `text-background-padding-x: 3` / `-y: 1`.
- **`symbol-avoid-edges` is dropped** on all six shield layers, so a shield can still land on a
  stub of road that MapLibre refuses to label. The zoom bands are the workaround, not a fix.
- **`text-max-angle`** on all five road-name layers, so a name follows a sharper bend here than in
  MapLibre.
- **Shields, thinned.** `shield-min-distance` is set from `shield-spacing` — 350 px here — because
  the decoder restarts spacing per feature, so the culler is what stops a road cut into many ways
  carrying a shield on each ([style-tools](../../docs/contributing/style-tools.md), "How far apart
  labels stay"). Since the culler started grouping per REF rather than per shield layer the density
  tracks MapLibre closely at z13–z14; the panes are framed differently, so it has not been counted
  exactly.
