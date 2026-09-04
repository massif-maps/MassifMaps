---
title: Style CLI
description: "massif-style — compile a CartoCSS project, or convert a MapBox/MapLibre style into one, with no toolchain to install."
sidebar_position: 1
---

# The style CLI

```bash
npx @massif-maps/style-tools mapbox2css style.json out/ --validate
```

`massif-style` is one command carrying every style conversion the project ships. It is an npm
package, so a style author needs no C++ toolchain, and the conversions that **must** agree with the
SDK run the SDK's own code compiled to WebAssembly — there is no second implementation to drift.

:::caution Not released yet
The package is built and tested in CI but has not been published to npm. Until it is, take
`massif-style.mjs` and `massif-style.wasm` from a `style-tools-v*`
[release](https://github.com/massif-maps/MassifMaps/releases), or build it from
[`tools/style-cli/`](https://github.com/massif-maps/MassifMaps/tree/master/tools/style-cli).
:::

| Subcommand | What it does |
|---|---|
| `css2xml <project.json> <out.xml>` | compiles a CartoCSS style project to the mapnik XML the decoder reads |
| `mapbox2css <style.json> <out-dir>` | translates a MapBox / MapLibre style JSON into a CartoCSS project |

Node 20 or newer. The wasm build uses `NODERAWFS`, so it runs under Node — not in a browser.

## Converting a MapBox or MapLibre style

```bash
massif-style mapbox2css style.json out/ --validate
```

Writes a style **project** — `project.json` plus the `.mss` files — which is exactly what a
[`"project"` style set](/docs/api/reference/styleset) loads:

```json
{"type": "vector",
 "source": "osm",
 "style": {"type": "mbvt",
           "project": {"type": "project",
                       "assets": {"type": "dir", "path": "/sdcard/my-style"},
                       "name": "style"}}}
```

Flags:

| | |
|---|---|
| `--validate` | compiles the result with `css2xml` in the same process, so CartoCSS the compiler would reject fails here rather than on device |
| `--strict` | exit non-zero if any MapBox property was dropped |
| `--shield-anchors [sides]` | let a POI name take the first **free** side of its icon, and draw the icon alone when none is (`shield-anchors` + `shield-text-optional`). Sides in preference order, default `right,left,top,bottom`; a layer stating its own `text-variable-anchor` keeps it |
| `--icon-font FACE`<br/>`--icon-font-map FILE` | draw every **shield** icon as a glyph of an icon font instead of a sprite, so the style ships one font rather than a sheet of PNGs. `FILE` maps `icon-image` names onto that face's characters (`{"mountain": ""}`, `"U+E90A"` and `59658` all read). A name it has no glyph for draws no icon, so country artwork is lost; a **marker** — a oneway arrow, a crossing — keeps its sprite |

`massif-style mapbox2css --help` lists the rest.

**Every conversion prints a coverage report** naming each property it could not carry and how often
it appeared. That is the number to read: a translation is not "done" because it produced files.

### What it cannot carry

These are CartoCSS gaps, not converter bugs — the report names them one by one:

- **Layer types with no symbolizer** — `heatmap`, and `symbol` layers that have only an icon.
- **Properties** — `line-blur`, `line-gap-width`, `line-gradient`, `fill-extrusion-pattern`, every
  `*-translate`, most `raster-*` adjustments.
- **Expressions with no CartoCSS form** — `feature-state`, `within`, `let`/`var`, `number-format`,
  `image`, `%`, `abs`/`floor`/`ceil`, and `interpolate` over anything other than zoom.
- **Draw order across source layers.** One entry in the project's `layers` array pulls *every*
  attachment of that name, so two MapBox layers on different source layers cannot be interleaved.
  The converter reports how many end up out of order rather than reordering silently.
- **The sprite.** `icon-image` is dropped, so a symbol layer with an icon and no text produces
  nothing.

## Compiling a CartoCSS project

```bash
massif-style css2xml project.json style.xml
massif-style css2xml --roundtrip project.json style.xml   # parse the XML back and diff it
```

This is the SDK's own compiler — `CartoCSSMapLoader` and `MapGenerator`, the same code the map runs
at runtime — so a project that compiles here renders there. Use it in CI to fail a style change
before it reaches a device.

## Writing the CartoCSS by hand

Every property the decoder understands, with its live/baked flag, is in the
[CartoCSS property reference](/docs/features/cartocss-properties). Properties marked **live** can be
changed through a [style parameter](/docs/features/style-parameters) with a repaint instead of a
re-decode.

## Also planned

- `carto2css` — mapnik XML back to CartoCSS. Not written; `MapGenerator` only goes one way.
- A browser build, for a playground that compiles a style in the page. `NODERAWFS` rules the
  current artifact out.

How the package is built, why the split between wasm and TypeScript, and how it is released:
[style tools internals](/docs/contributing/style-tools).
