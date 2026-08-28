---
title: Labels, Shields & Font Icons
sidebar_position: 10
---

# Labels, Shields and Font Icons

CartoCSS label properties the fork added: a name that takes the free side of its icon, SDF font
icons, rounded plates behind text and icon, callout labels for peaks, and per-label distance limits.

:::info Fork feature
Added in PRs [#57](https://github.com/massif-maps/MassifMaps/pull/57) and
[#56](https://github.com/massif-maps/MassifMaps/pull/56), with the renderer half in
`massif-maps/massif-maps-libs`. Everything here is style syntax — no API call is needed.
Technical notes: [`docs/internals/rendering/06-labels.mdx`](/docs/internals/rendering/labels).
:::

<figure class="docs-figure">

![POI names placed on the free side of font icons](/img/features/shield-labels.jpg)

<figcaption>~2300 live POI labels: each name takes the first free side of its icon, and falls back to the icon alone where nothing fits (<code>shield-text-optional</code>). Icons are single SDF glyphs of an icon font.</figcaption>

</figure>

## Anchored shields: the name takes a free side

A shield is one label whose glyph run is `[icon] [text]`; only the text moves.

```css
#poi {
  shield-name: [name];
  shield-file: url(shields/place.svg);       /* a bitmap icon, as before */
  /* AND/OR a font icon: the string holds the icon face's own character (a PUA codepoint, e.g. U+E990) */
  shield-icon-name: '<icon character>';
  shield-icon-face-name: 'osm';
  shield-icon-size: 15;
  shield-icon-fill: #b5651d;
  shield-anchors: 'right,left,top,bottom';   /* sides, in preference order */
  shield-text-optional: true;                /* no side free -> draw the icon alone */
  shield-text-dx: 2;                         /* gap from the icon, MIRRORED per side */
  shield-text-horizontal-alignment: 'auto';  /* justify wrapped lines with the chosen side */
}
```

- The culler tries the side the label already holds, then the style's order, and takes the first
  free one — keeping the current side is what stops names swapping sides under a moving camera.
- The text is placed against the icon's **edge**, so `dx`/`dy` become a gap pushed away from the
  icon and the style does not have to mirror its own alignment.
- `shield-text-optional` costs one more layout variant, not a second label.
- Placement re-runs when the camera zooms by a quarter of a level (it used to run only on tile-set
  changes, so zooming in never gave a fallen-back name its text back).
- Insertion is greedy: a name may take the space a lower-ranked neighbour's icon needed, and that
  icon is hidden. `shield-placement-priority` is the lever over who wins; a bigger `shield-size`
  sorts earlier among equal priorities.

A style without `shield-anchors` builds no variants and takes exactly the old path. With every POI
anchored (~2300 labels, 4 sides + icon-only) a placement pass measured **20.8 ms against 16.3 ms**
unset, and frame time moved by under 1 ms.

## Repeating a shield along its road

`shield-placement` / `text-placement` decide two things at once — how often the label is placed along
a line, and whether its glyphs follow the line's direction:

| Placement | Repeats at `spacing` | Glyphs |
|---|---|---|
| `point` | no — one label per line | flat on the ground, swivelling to face the camera |
| `billboard` | no — one label per line | upright, facing the camera |
| `line` | yes | flat on the ground, following the line |
| `billboard-line` | yes | upright, following the line |
| `billboard-line-repeat` | yes | upright, **not** following the line |

`billboard-line-repeat` is the road-shield combination and is what **shields default to**: a route
number repeated along its motorway has to stay readable, not turn with the bend. On a feature that
is not a line it behaves as `billboard`.

```css
#road_shield {
  shield-name: [ref];
  shield-file: url(shields/motorway.svg);
  shield-spacing: 200;                       /* ground distance between repeats */
  shield-min-distance: 100;                  /* ... and the screen-space guarantee */
  /* shield-placement: billboard-line-repeat;   the default; 'point' is the old one */
}
```

Text still defaults to `point`. `text-spacing: 0` means one label for the whole line.

## Font icons

`shield-icon-name` is a run of glyphs from an icon face, drawn before the text with its own colour
and size. They are **SDF like the text**, so they stay sharp at any zoom and cost one atlas cell
each — not a bitmap. A shield may carry both: `shield-file` is the first prefix glyph and the icon
run follows it, side by side.

If a style packages no font at all, labels no longer fail: the SDK falls back to the **system
fonts** (`/system/fonts` on Android, CoreText on iOS/macOS, DirectWrite on UWP), with `Arial` mapped
to the platform default. Style-packaged fonts always keep precedence.

## Font lists

A face name is a CSS-like list, most preferred first, and an entry may say which platform it is for:

```css
#poi {
  text-face-name: "Roboto, Helvetica Neue, sans-serif";              /* one string */
  text-face-name: "android:Roboto", "ios:Helvetica Neue", "Arial";   /* or a CSS list */
}
```

`android:`, `ios:`, `macos:` and `windows:` are the tags; an entry tagged for another platform is
dropped, an untagged one is kept everywhere. The first name the device has a font for becomes the
main font and the rest are its glyph fallbacks. Resolution is cached, so a list costs nothing per
tile.

The same list works for the font names of the **vector elements** — `BalloonPopupStyleBuilder`
(title, description), `BalloonPopupButtonStyleBuilder` and `TextStyleBuilder` — which previously
ignored any name the platform did not know verbatim:

```java
BalloonPopupStyleBuilder builder = new BalloonPopupStyleBuilder();
builder.setTitleFontName("Roboto, Helvetica Neue, sans-serif");
```

## Plates behind the text and the icon

```css
#road_label {
  text-background-fill: #ffffff;
  text-background-opacity: 0.85;
  text-background-radius: 3;
  text-background-padding-x: 4;
  text-background-padding-y: 2;
  text-background-border-fill: #444444;
  text-background-border-width: 1;
}
```

The same properties exist as `shield-background-*` (behind the text) and `shield-icon-background-*`
(behind the icon run). A plate is nine-sliced from one cached atlas cell that carries both the fill
and the border, drawn as a single quad: the border never shows through the fill, so a plate keeps
its colour while the label fades in and at any `text-background-opacity`. Plates are part of what
the label covers, so the culler tests the plated box.

Measured with a plate behind name **and** icon on ~2100 labels: about **+1 ms per frame**, no change
in draw-call count. A screen of road shields — tens, not thousands — is far below the noise.

## Halos are outlines

`text-halo-*`, `shield-halo-*` and `shield-icon-halo-*` draw **around** the glyph, not behind it —
the glyph's own shape is cut out of the halo. So a label keeps its colour while it fades in, and a
translucent `text-fill` (or `text-opacity`) shows the map through it rather than the halo. A style
that relied on an opaque halo filling in behind a transparent fill now gets a hollow outline; give
the fill its own colour instead.

## Callout labels

A panorama has hundreds of summits inside a few degrees of the horizon, all wanting the same band of
pixels. `text-placement: callout` lifts the name off its anchor in **screen pixels** and joins it
back with a leader line; a name that loses its row steps to the next one instead of being hidden.

```css
#mountain_peak {
  text-name: [name];
  text-secondary-name: [ele] + 'm';           /* one label, two type sizes */
  text-secondary-scale: 0.62;
  text-placement: callout;
  text-callout-screen-anchor: 0.25;           /* band, as a fraction of screen height from the top */
  text-callout-step: -18;                     /* pixels per row; negative stacks DOWNWARDS */
  text-callout-max-rows: 6;
  text-callout-line-anchor: bottom-left;      /* the point held over the summit */
  text-callout-align: top-right;              /* the point put on the band line */
  text-callout-persist: 2;                    /* passes a visible name may fail before it hides */
  text-min-distance: 20;
  text-placement-priority: [ele];             /* the higher summit claims the row … */
  text-rank: 0 - [view::distance]/100;        /* … and the nearer of two equals wins it */
}
```

- `view::distance` is metres from the camera to the label, evaluated per label per placement pass
  and **added to the priority**. It is defined in this expression only, and it never changes how a
  label looks — only which of two colliding labels keeps its slot.
- Write `0 - x`, not `-x`: a leading minus in front of a field parses as the literal string `"-"`
  and the declaration silently fails.
- A callout must fit on the screen **above** its feature or it loses its name — a summit already
  high in the frame has nowhere to put one.
- Unlike every other orientation, a callout is not dropped when the view meets the surface edge-on,
  which is what makes it work at a near-zero or negative tilt.

## Ranking the app owns

The `[rank]` a vector tile ships is one editor's idea of what matters, and an outdoor app wants a
different one. Put the ranking in a [table style parameter](style-parameters.md) instead of in the
style, and the app writes it:

```json
"styleparameters": {
  "poi_rank": { "default": { "peak": 900, "alpine_hut": 800, "supermarket": 50, "zoo": 40 } }
}
```

```css
#poi {
  marker-placement-priority: get([param::poi_rank], [class], 100);
  text-placement-priority:   get([param::poi_rank], [class], 100);
}
```

```java
decoder.setStyleParameter("poi_rank", outdoorRanking);   // one call swaps the whole ranking
```

The lookup is resolved while the tile is decoded, so it costs nothing per frame. Changing the table
re-decodes the visible tiles, which suits a profile the user picks — not a slider.

## Distance limits

Cut labels that are too far to be useful (metres; `0`, the default, means no limit):

```css
#transportation_name { text-max-distance: 2000; }
#poi                 { marker-max-distance: 800; }
#shield              { shield-max-distance: 1500; }
```

## Sharpness

Glyph rasters follow a ladder (16 / 28 / 40 px) and the SDF is coverage-based (FreeType's `bsdf`),
which is what removed the blurred stems and the holes at stem/shoulder joins that an outline-based
SDF produced. Nothing to configure — but it is why label appearance changed relative to older
builds of this fork.

## 2D and 3D from one style

`text-orientation-mode` is fixed when the tile is decoded, so it cannot be switched by a style
parameter — branch on the SDK's own
[`render::3d`](/docs/features/style-parameters#the-render-mode-variable-render3d) instead:

```css
#road_label { text-orientation-mode: [render::3d] ? billboard-line : line; }
```

## See also

- [Composite Vector Tile Layer](/docs/features/composite-vector-tile-layer) — where a label group sits in the draw order.
- [Live Style Parameters](/docs/features/style-parameters) — change label colours without re-decoding tiles.
