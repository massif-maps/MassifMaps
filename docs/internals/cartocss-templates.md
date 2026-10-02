---
title: CartoCSS templates
description: "Templates, @extend and display: none - how a child CartoCSS project widens or narrows a base style's rules, how it compiles, and the planned data-driven and run-time layers."
sidebar_position: 6
---

# CartoCSS templates

Scope: the CartoCSS compiler's template support (`libs-massif/cartocss`) — the syntax, what it
compiles to, and the two layers planned on top of it. How a child project is set up is in
[Extending Massif](../styles/massif-extending.md); parameters that change a style without
recompiling it are in [Style parameters](../features/style-parameters.md).

## The problem

A child project `extends` a base and appends stylesheets. The CartoCSS cascade already lets an
appended rule **refine** the base: `#poi[class = 'bakery'] { text-placement-priority: 5; }` is
crossed with every base rule it intersects, at compile time.

It could not do the other two things a child needs:

| Override | Before templates |
|---|---|
| **refine** — change a property where the base draws | works |
| **widen** — draw a feature where the base's filter excludes it (a bakery from z14 when the base starts POIs at z17) | dropped: the new rule has no `text-name` or face, those live in the base rule, so no symbolizer is built |
| **narrow** — stop drawing what the base draws (bus stops before z17) | impossible: an empty rule builds nothing, and the base rule still matches |

## Syntax

```css
/* a template: a block with no selector, drawn only where a rule extends it */
%poi-badge {
  text-name: [name];
  text-face-name: @font_medium;
  text-size: 12;
}

#poi[zoom >= 15][rank < 7]::poi              { @extend %poi-badge; }
#poi[zoom >= 16][rank >= 7][rank < 20]::poi { @extend %poi-badge; }
#poi[zoom >= 17][rank >= 20]::poi            { @extend %poi-badge; }
```

A child project widens, narrows and restyles in its own stylesheet, listed after the base's:

```css
/* widen: bakeries from z14, winning their collisions */
#poi[class = 'bakery'][zoom >= 14]::poi { @extend %poi-badge; text-placement-priority: 5; }

/* narrow: no bus stop before z17 */
#poi[class = 'bus'][zoom < 17]::poi { display: none; }

/* restyle: every rule extending the template changes */
%poi-badge { text-name: [name]; text-face-name: 'Noto Sans Bold'; text-size: 13; }
```

## Semantics

- **The rule's own declarations win** over the template's, wherever the `@extend` is written in
  the block. The compiler keeps the first declaration of a field per block, and expands templates
  after the block's own declarations.
- **An extended declaration ranks as if written at its `@extend`**: its specificity order is the
  `@extend`'s, not the template's line. A child's rule extending a base template therefore ranks
  as a child declaration.
- **The last definition of a template wins**, for every rule extending it, wherever that rule is.
  A child's stylesheets come after the base's, so redefining a template restyles the base.
- Templates are top-level only. A template may extend other templates and may contain nested
  rules; nested rules keep their own declaration order.
- An undefined template and a template that extends itself fail the load
  (`Undefined template %name`, `Template %name extends itself`), rather than drawing nothing.
- **Widening needs the same attachment.** Where the widened rule and the base rule both match
  (the bakery at z17), the cascade merges them into one property set, so the feature is drawn
  once. On another attachment it would be drawn twice.
- **`display: none`** keeps the rule with no symbolizer. Every CartoCSS attachment compiles to a
  mapnik style with `filter-mode="first"`: a feature is drawn by the first rule that matches it,
  so an empty first match hides it from every rule after it. The property cascades like any
  other: a later or more specific rule can set `display: auto` to show the feature again.

## How it compiles

Everything happens in `CartoCSSCompiler::buildPropertyLists`, before the zoom loop:

1. Collect the templates, the last definition per name winning.
2. While a rule's block is turned into properties (`buildBlockProperties`), its own declarations
   are added first; each `@extend` is then expanded with the rule's filters and attachment, and
   the `@extend`'s order.
3. `PropertySet::isSuppressed()` marks a set whose `display` is `none`;
   `CartoCSSMapnikTranslator::buildRule` builds it as a rule with a filter and no symbolizer, and
   `CartoCSSMapLoader` keeps it where it drops every other empty rule.
4. `MapGenerator` writes an empty rule out when it was built empty, so the compiled XML flavour
   keeps the suppression. A rule whose symbolizers all serialise to nothing is still dropped.

**Cost.** Nothing at run time: templates and `display` are resolved while the style compiles, and
the decoder sees ordinary mapnik rules. A style that uses neither compiles byte-identical to
before: the five Massif variants and the `osm` and `custom` children, `css2xml` on `master` and on
this branch, `cmp` equal. Compiling the whole Massif project takes 0.19 s (`css2xml` native
Release, M5 Pro).

Tests: `tests/style/CartoCSSTemplateTest.cpp` — inheritance and its precedence, widening,
redefinition, `display: none` / `auto`, the two load errors.

## Zoom constants

A rule's zoom range may be a project constant, resolved when the style compiles:
`#poi[zoom >= $poi_bus_minzoom][class = 'bus']`. The compiler substitutes the constant at each
zoom of its loop, so the selector compiles to the same scale range as a literal, and a child
project moves it in its `constants` (`extends` merges them key by key,
`CartoCSSMapLoader::loadMapDocument`) without writing a rule. Not supported: a FEATURE field
compared with a constant, `[rank < $name]`, which compares the field with the constant's name.

A shield with an icon and an empty name compiles to an icon-only shield (it was dropped as a
nameless text): a bus stop's badge before its name.

## How Massif uses it

`mapbox2css` writes them from layer metadata — `massif:template`, `massif:attachment`,
`massif:minzoom-const` / `massif:maxzoom-const`, see
[style-tools](../contributing/style-tools.md#a-property-maplibre-will-not-accept-goes-in-metadata). Every POI
layer of `styles/massif/layers/pois.py` extends `%poi` and draws into `::poi`: the template is each
property all of them state, at its most common value, and a rule keeps what differs (its filter,
priority, a fixed palette). The base is OpenFreeMap Liberty's rank ladder plus the stations, the
airports and the bus stop; the bus stop's two zooms are constants. `examples/osm/` overrides both
ways — bakeries from z15 and pharmacies held to z17 with rules, the bus stop at z15 / z16 with
`constants` — checked on the style preview (`?compare=/styles/massif/carto/&compareVariant=osm`,
the SDK web build of this branch, Grenoble z15.5–16.3, local tiles).

## How the references do it

- **Tangram** builds the override into the scene's structure: a layer's sublayers inherit its
  draw rules and override them, `import:` deep-merges scenes, and `global.x` is substituted
  anywhere. At run time `SceneUpdate(path, value)` reloads and rebuilds the scene. Per-feature
  logic is JavaScript (Duktape or JavaScriptCore, `core/src/js` in tangram-ng), flexible but paid
  per feature.
- **MapLibre / Mapbox GL** have no child styles; a style is patched as JSON. Mapbox Standard's
  `config` and `imports` are the closest thing, and only reach what the base exposes as config.
- **carto.js** (the original CartoCSS compiler) has variables and nesting, no mixins.
- **Sass `@extend`** merges the extending selector into the template's rule. Here the template's
  declarations are copied into the extending rule instead, which is what lets a child widen a
  filter: the copy carries the child's selector, not the base's.

## Planned: overrides as data

A template rule with parameters, expanded once per entry of a project `constants` object:

```json
"constants": { "poi_overrides": {
  "bakery":   { "minzoom": 14, "priority": 1000000 },
  "pharmacy": { "priority": -1000000 },
  "bus":      { "minzoom": 17, "label_minzoom": 18 } } }
```

```css
@each poi_overrides as $class, $o {
  #poi[class = $class][zoom >= $o.minzoom]::poi { @extend %poi-badge; text-placement-priority: $o.priority; }
  #poi[class = $class][zoom < $o.minzoom]::poi  { display: none; }
}
```

Only the entries present generate rules. `extends` already merges `constants` key by key
(`CartoCSSMapLoader::loadMapDocument`), so a child adds entries without restating the base's. The
pieces exist: a constant in a property (`$name`) and in a selector (`[$name = 1]`,
`[zoom >= $name]`) resolves at compile time today.

## Planned: changes at run time

An app call — `setStyleConstants({...})` or a facade path — that recompiles the CartoCSS and
re-decodes the tiles, the cost of setting a non-live style parameter today. The compiled XML
flavour carries no templates and stays a frozen snapshot; run-time overrides need a CartoCSS
flavour.

## What could be better

- The converter's template is per-property majority: a layer group with no common majority
  (half the layers on one palette, half on another) keeps more inline than it needs to.
- A suppression is emitted for every zoom the narrowing rule covers, also where the base draws
  nothing (`[class = 'bus']` from z0); harmless — one extra filter test per feature — but
  avoidable.
- `@extend` copies declarations, so each extending rule still compiles to its own property sets;
  it saves writing, not compiling.
