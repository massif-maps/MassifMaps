# What no open set covers

9 of the 211 names. Drop an SVG named `<sprite-name>.svg` into `local/` and re-run `collect.mjs`.

The 13 transit marks that used to be here came from Wikimedia Commons — `node fetch-commons.mjs`
downloads them, `local/SOURCES.json` records what each one is. All 21 composites resolve now that
their parts do.

| | |
|---|---|
| `svg/` | **164** — 144 maki, 7 temaki, 13 Commons |
| letters | **17** — content is a character, no artwork |
| composites | **21** — `.`-separated, composed from the parts |
| **missing** | **9** |

## Still missing

| sprite | why |
|---|---|
| `chongqing-rail-transit` | Commons carries its per-LINE icons only, no system mark |
| `stairs` | indoor. `openstreetmap/map-icons` has `svg/transport/steps.svg` to redraw from |
| `escalator` | indoor. Nothing anywhere; temaki has `elevator` and nothing else |
| `ramp` | indoor. temaki's `boat_ramp` is a different thing |
| `airport-check-in` | airport |
| `lounge` | airport. temaki's `lounger` is a sun-lounger |
| `highway-services` | motorway service area. `classic.small/vehicle/services.svg` to redraw from |
| `toilet-male` | maki has unisex `toilet` only. `svg-twotone/public/toilets/men.svg` to redraw from |
| `toilet-female` | as above |

Six of the eight generic ones are reached only through a whole-sheet lookup or `indoor-label`, so
the cost is indoor POIs and little else. [Temaki](https://github.com/massif-maps/temaki) is where
they belong — CC0, 15 px, maki's drawing style, and the fork is ours.

`chongqing-rail-transit` is the one transit mark left; a generic metro glyph is a fair answer, and
it costs recognisability rather than correctness.

## `openstreetmap/map-icons` is a tracing reference, not a source

997 SVGs, and it has a shape for four of the eight above. None can be copied in: they are multi-path
and **multi-colour** (`#0031ff`, white, black), carry Inkscape metadata, and sit on 480 / 580 / 200 /
16 px canvases with no shared viewBox — against maki's single-path monochrome 15×15. The two-tone
ones break worst: a glyph is one colour, so their white counter-shapes would merge into the fill.

It has nothing for any transit mark — [OSM Carto ships no country-specific transit symbols by
policy](https://wiki.openstreetmap.org/wiki/Proposed_features/Customized_Icons_for_Public_Transport),
which is this same trademark question in a different place. Its README says a PD-style licence is
*expected* of new icons: an expectation, not a grant, over two decades of contributions.

## What the Commons downloads still need

They are whole badges, in colour, on their own canvases. Turning each into a glyph is by hand.

**Three are the wrong shape for a 15 px box** — a wordmark does not survive being 15 px wide:

| sprite | aspect | |
|---|---|---|
| `new-york-subway` | **6.78** | the MTA wordmark. The circular route bullet is the icon you want |
| `tokyo-metro` | 2.03 | logo + wordmark; the `M` ligature alone is square |
| `san-francisco-bart` | 1.64 | the `bart` wordmark |

**Keep the ring** on `london-underground`, `london-overground`, `london-dlr`, `london-tfl-rail` and
`gb-national-rail`: there the ring or the arrow *is* the mark, not a background the style draws.
Everywhere else the disc goes, and `shield-icon-background-fill` replaces it.

**Flattening to one colour needs care** where a shape is a white counter rather than a gap —
`london-tfl-rail` (10 shapes), `osaka-subway` (17), `tokyo-metro` (11), `taipei-metro` (16). Union
them and the counter fills in; subtract them and it stays. `gb-national-rail` is the easy one: 259
bytes, two shapes, one colour.
