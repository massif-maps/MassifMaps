# Icon font sources

One SVG per name in `sprites.txt`, gathered into `svg/` so an icon font can be built from a folder
whose **file names are the style's `icon-image` names**. `massif-style mapbox2css --icon-font` then
resolves a shield's icon through that font instead of a sprite sheet.

```sh
node tools/icon-font/collect.mjs [--maki DIR] [--temaki DIR]
```

Defaults to `/Volumes/dev/carto/maki/icons` and `/Volumes/dev/carto/temaki/icons`. Writes `svg/` and
`MANIFEST.json`; neither is hand-edited.

| | |
|---|---|
| `sprites.txt` | the 211 names to cover — Mapbox Standard's sheet, minus what does not need a glyph |
| `sources.json` | the aliases and the letters. Hand-edited; everything else is derived |
| `local/` | hand-picked or hand-drawn SVGs, named after the sprite. **Wins over every set** |
| `svg/` | the output |

## Only the content

A transit roundel's disc, its ring and all three of its colours are **style** properties in the SDK —
`shield-icon-background-fill`, `-border-fill`, `shield-icon-fill` — and Mapbox Standard already
carries them per feature for its generic networks. So a glyph that drew its own badge would fight
the style. What goes in `svg/` is the **content**: the letter, the pictogram, the mark itself.

The exception is a mark whose *ring* is the logo rather than a background — the London roundel, the
National Rail double arrow. There the ring is part of the drawing and has to stay in the glyph.

## What is not a glyph

- **Road shields.** Massif generates those; every `<name>-<ref-length>` sprite and the US/CA toll
  plates are out of `sprites.txt` entirely (382 + 19 names).
- **Markers.** A oneway arrow and a crosswalk are not labels and have no glyph run, so they keep
  their sprite: `crosswalk-large/small`, `oneway-large/small`.
- **Letters.** 17 roundels whose content is just a character (`paris-metro` is `M`, `de-s-bahn` is
  `S`, `paris-rer` is `RER`, `moscow-metro` is Cyrillic `М`). Those go in the font map as that
  character and need no artwork — see `letter` in `sources.json`.
- **Composites.** 21 names are several roundels in a row (`gb-national-rail.london-dlr.london-
  underground`). `shield-icon-name` is a string shaped into a glyph *run*, so the converter can split
  on `.` and concatenate the parts. Nothing is drawn for them, but every part has to be covered.

## Licences

`maki` and `temaki` are both **CC0 1.0**. Anything added to `local/` carries its own — record it in
`MISSING.md` beside the name, and remember that a mark can be public domain for copyright while
still being a registered trademark (RATP, SNCF, IDFM, TfL). Labelling that network's own stations is
the nominative case; reuse beyond it is a separate question.
