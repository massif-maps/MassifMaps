---
title: Branding assets
description: "The one mark, the generator that derives every asset from it, and what still has to be uploaded by hand."
---

# Branding assets

Every Massif Maps mark — the website logo and favicon, the social card, the GitHub avatar, both
demo launcher icons — comes out of a single geometry definition in
[`scripts/gen-branding.mjs`](https://github.com/massif-maps/MassifMaps/blob/master/scripts/gen-branding.mjs).
**Nothing in the table below is edited by hand**; change the mark in the script and re-run it.

```sh
npm --prefix scripts i @resvg/resvg-js   # once; scripts/node_modules is gitignored
node scripts/gen-branding.mjs
```

Without `@resvg/resvg-js` the script still writes every SVG and skips the PNGs, with a warning.

## The mark

Two peaks on a 512 grid, baseline at `y=402`. Each peak carries a lit face and a shaded face, and
contour lines are drawn at elevations **shared by both peaks** — that is the detail that makes it
read as terrain instead of as bunting. Palette: teal `#12E3CF`/`#00736A`, blue `#4FA3F7`/`#063877`,
tile `#FFFFFF` → `#EAF1F7`, horizon glow `#1FA9C9`.

The tile is **white** — the peaks are saturated enough to carry themselves, and they read brighter
on it than on a dark one. Only the social card keeps a night-sky background, with the white tile
sitting on it.

The mark is placed by transform in three framings, and they are not interchangeable:

| Framing | Mark width | Why |
|---|---|---|
| `TILE` — app icon, avatar, card | 70% of the tile | iOS and the avatar mask lightly; padding is what makes it read as an app icon |
| favicon | 91% of a square crop, **no tile** | a favicon is looked at 16px wide; the peaks get the whole of it instead of sharing it with a background, and transparency works on a light and a dark browser tab |
| `ADAPTIVE` — Android adaptive icon | 57% of 432 | see below |

## The Android safe zone is smaller than it looks

An adaptive icon layer is 108dp (432px), but only the **central 72dp (288px)** survives, and a
circular mask keeps only a 288px-diameter circle of that. The mark's widest points are its two base
corners, so it is not simply centred: it is lifted until the base sits at `y=258`, which puts those
corners at radius 131 of the 144 available. Centring it (base at `y=312`) puts them at radius 163
and a circular launcher slices both corners off.

The icon ships four Android pieces: `ic_launcher_background.xml`, `ic_launcher_foreground.xml`,
`ic_launcher_monochrome.xml` (Android 13 themed icons tint a flat silhouette, so it carries no
faces and no contours) and the `mipmap-anydpi-v26` adaptive XML that binds them. API 21–25 never
reads those and gets the rasterised `mipmap-*/ic_launcher.png` + `ic_launcher_round.png` instead.

Gradients in a `VectorDrawable` need API 24, which is below the API 26 that reads an adaptive icon
at all — so they are safe here even though the demo's `minSdkVersion` is 21.

One trap, paid once: Android Studio's template also puts a foreground in **`res/drawable-v24/`**,
which outranks `res/drawable/` on every API that reads an adaptive icon. Writing the generated one
to `drawable/` therefore changed nothing until that file was deleted. There must be exactly one
`ic_launcher_foreground.xml` in the tree.

On iOS, `Assets.xcassets` is picked up by the existing `- path: MassifDemo` source entry, but the
`ASSETCATALOG_COMPILER_APPICON_NAME: AppIcon` setting in `scripts/ios-dev/project.yml` is what
binds it — and the project has to be regenerated (`scripts/ios-dev/regen.sh`) for either to apply.

## What the script writes

| Path | What |
|---|---|
| `media/branding/mark.svg` | the mark alone, transparent |
| `media/branding/icon.svg` | the 1024 icon tile |
| `media/branding/github-avatar.png` | 1024 PNG, **uploaded by hand** in the org/repo settings |
| `website/static/img/logo.svg` | navbar logo |
| `website/static/img/favicon.svg` | favicon, transparent |
| `website/static/img/social-card.svg` / `.png` | og:image |
| `scripts/android-dev/app/src/main/res/drawable/ic_launcher_{background,foreground,monochrome}.xml` | adaptive layers |
| `scripts/android-dev/app/src/main/res/mipmap-anydpi-v26/ic_launcher{,_round}.xml` | adaptive binding |
| `scripts/android-dev/app/src/main/res/mipmap-*/ic_launcher{,_round}.png` | API 21–25 fallback, 48–192px |
| `scripts/ios-dev/MassifDemo/Resources/Assets.xcassets/AppIcon.appiconset/` | 1024 PNG + `Contents.json` |

`docusaurus.config.js` points `image:` at the **PNG** card. Twitter, Slack and LinkedIn do not
rasterise an SVG `og:image`; the SVG is kept as the editable artefact and for the site itself.

## The card names no platform brands

The platform row is pills — a coloured dot and a word — not the Android robot or the Apple logo.
Both are trademarks with usage terms, and the row has to keep working when the third platform
arrives; `+ more` is a dashed pill, so adding one is adding an entry to `PILLS`.

## Known gaps

- The launcher icons are verified by rasterising an SVG that mirrors the `VectorDrawable`
  transforms, plus an `aapt2 compile` of the whole `res/` tree. That is not a render of the actual
  `VectorDrawable`, and neither demo's icon has been looked at on a device or a simulator.
- No dark/light variants: the tile is white in both themes, and the transparent favicon relies on
  the peaks staying legible on a dark browser tab — the shaded blue face nearly disappears there.
- The GitHub avatar and the repo social preview are manual uploads; nothing checks that the ones
  in use match `media/branding/`.
