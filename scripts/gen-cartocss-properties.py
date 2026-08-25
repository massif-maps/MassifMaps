#!/usr/bin/env python3
"""Generate the CartoCSS property reference from the mapnikvt symbolizers.

The set of properties a style may use is not written down anywhere: it lives in the
bindProperty() calls across libs-massif/mapnikvt/src/mapnikvt/*Symbolizer.h and in
CartoCSSMapnikTranslator's cartocss -> mapnik name map. This reads both and emits

  docs/features/cartocss-properties.md      the human reference
  tools/style-cli/src/generated/properties.json   the converter's allowlist

so mapbox2css cannot drift from what the decoder actually accepts.

Two flags come out of the C++ and matter more than the names:

  live   the value is re-evaluated per frame, so a param:: driving it changes with a redraw.
         Only *FunctionProperty types qualify - Property::isLiveCapable() is defined on
         GenericFunctionProperty alone.
  baked  bindProperty's third argument. The property is also read while the tile is built, so a
         param:: reaching it forces a re-decode however live the type is.

Usage: python3 scripts/gen-cartocss-properties.py [--check]
"""

import argparse
import json
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SYMBOLIZER_DIR = REPO / "libs-massif" / "mapnikvt" / "src" / "mapnikvt"
TRANSLATOR = REPO / "libs-massif" / "cartocss" / "src" / "cartocss" / "CartoCSSMapnikTranslator.cpp"
MARKDOWN_OUT = REPO / "docs" / "features" / "cartocss-properties.md"
JSON_OUT = REPO / "tools" / "style-cli" / "src" / "generated" / "properties.json"

# Symbolizer itself has no base, and it is the one that binds comp-op for everything else.
CLASS_RE = re.compile(r"class\s+(\w+)\s*(?::\s*public\s+(\w+))?\s*\{")
BIND_RE = re.compile(r"bindProperty\(\s*\"([^\"]+)\"\s*,\s*(?:&(\w+)|nullptr)\s*(?:,\s*(true|false)\s*)?\)")
UNBIND_RE = re.compile(r"unbindProperty\(\s*\"([^\"]+)\"\s*\)")
MEMBER_RE = re.compile(r"^\s*(\w*Property)\s+(_\w+)\s*(?:=\s*\w*Property\((.*?)\))?\s*;", re.MULTILINE)

SYMBOLIZER_LIST_RE = re.compile(r"_symbolizerList\s*=\s*\{(.*?)\};", re.DOTALL)
PROPERTY_MAP_RE = re.compile(r"_symbolizerPropertyMap\s*=\s*\{(.*?)\n\s*\};", re.DOTALL)
MAP_ENTRY_RE = re.compile(r"\{\s*\"([^\"]+)\"\s*,\s*\"([^\"]*)\"\s*\}")

# Value kind shown in the reference. Derived from the Property class name, which is what decides
# how a style's expression is evaluated.
TYPE_KINDS = {
    "ColorFunctionProperty": "color",
    "FloatFunctionProperty": "float",
    "ColorProperty": "color",
    "FloatProperty": "float",
    "BoolProperty": "bool",
    "StringProperty": "string",
    "ValueProperty": "value",
    "TransformProperty": "transform",
    "CompOpProperty": "comp-op",
    "LineJoinModeProperty": "enum",
    "LineCapModeProperty": "enum",
}


def parse_symbolizers():
    """class name -> {'base': str, 'binds': {property: member}, 'unbinds': set, 'members': {member: (type, default)}}"""
    classes = {}
    for path in sorted(SYMBOLIZER_DIR.glob("*.h")):
        text = path.read_text(encoding="utf-8")
        match = CLASS_RE.search(text)
        if not match:
            continue
        name, base = match.group(1), match.group(2) or ""
        binds, baked = {}, set()
        for prop, member, baked_flag in BIND_RE.findall(text):
            binds[prop] = member  # empty when bound to nullptr: a name accepted and ignored
            if baked_flag == "true":
                baked.add(prop)
        members = {m: (t, (d or "").strip()) for t, m, d in MEMBER_RE.findall(text)}
        classes[name] = {
            "base": base,
            "file": path.name,
            "binds": binds,
            "baked": baked,
            "unbinds": set(UNBIND_RE.findall(text)),
            "members": members,
        }
    return classes


def clean_default(text):
    """'1.0f' -> '1.0', '"#000000"' -> '#000000'. The C++ literal suffix is noise in a reference."""
    text = text.strip().strip('"')
    return text[:-1] if re.fullmatch(r"-?\d+(\.\d+)?f", text) else text


def resolve(classes, name):
    """Flatten a symbolizer's own binds over its bases'. Returns {property: record}."""
    chain, cursor = [], name
    while cursor in classes:
        chain.append(cursor)
        cursor = classes[cursor]["base"]

    resolved = {}
    for cls in reversed(chain):  # base first, so a subclass overrides
        info = classes[cls]
        for prop in info["unbinds"]:
            resolved.pop(prop, None)
        for prop, member in info["binds"].items():
            kind, default = info["members"].get(member, ("", ""))
            resolved[prop] = {
                "type": kind,
                "kind": TYPE_KINDS.get(kind, "value"),
                "default": clean_default(default),
                # Property::isLiveCapable() is overridden on GenericFunctionProperty only.
                "live": kind.endswith("FunctionProperty"),
                "baked": prop in info["baked"],
                "declaredIn": cls,
            }
    return resolved


def parse_translator():
    text = TRANSLATOR.read_text(encoding="utf-8")
    symbolizers = re.findall(r"\"([^\"]+)\"", SYMBOLIZER_LIST_RE.search(text).group(1))
    entries = MAP_ENTRY_RE.findall(PROPERTY_MAP_RE.search(text).group(1))
    return symbolizers, entries


# The cartocss symbolizer prefix -> the mvt class the translator instantiates (createSymbolizer).
SYMBOLIZER_CLASSES = {
    "point": "PointSymbolizer",
    "line": "LineSymbolizer",
    "line-pattern": "LinePatternSymbolizer",
    "polygon": "PolygonSymbolizer",
    "polygon-pattern": "PolygonPatternSymbolizer",
    "marker": "MarkersSymbolizer",
    "text": "TextSymbolizer",
    "shield": "ShieldSymbolizer",
    "building": "BuildingSymbolizer",
    "raster": "RasterConfigSymbolizer",
    "hillshade": "HillshadeConfigSymbolizer",
    "contour": "ContourConfigSymbolizer",
}


def build(classes, symbolizer_list, entries):
    properties, unmapped = [], []
    for cartocss_name, mapnik_name in entries:
        # Longest prefix wins: 'line-pattern-file' is line-pattern's, not line's.
        prefix = max(
            (s for s in symbolizer_list if cartocss_name.startswith(s + "-")),
            key=len,
            default=None,
        )
        if prefix is None or prefix not in SYMBOLIZER_CLASSES:
            unmapped.append((cartocss_name, mapnik_name, "no symbolizer prefix"))
            continue
        if not mapnik_name:
            # Bound to nothing on purpose - the translator accepts the name and drops it.
            properties.append({
                "cartocss": cartocss_name, "mapnik": None, "symbolizer": prefix,
                "kind": "ignored", "type": "", "default": "", "live": False, "baked": False,
            })
            continue

        resolved = resolve(classes, SYMBOLIZER_CLASSES[prefix])
        record = resolved.get(mapnik_name)
        if record is None:
            unmapped.append((cartocss_name, mapnik_name, f"not bound by {SYMBOLIZER_CLASSES[prefix]}"))
            continue
        properties.append({
            "cartocss": cartocss_name,
            "mapnik": mapnik_name,
            "symbolizer": prefix,
            "kind": record["kind"],
            "type": record["type"],
            "default": record["default"],
            "live": record["live"] and not record["baked"],
            "baked": record["baked"],
        })
    properties.sort(key=lambda p: (p["symbolizer"], p["cartocss"]))
    return properties, unmapped


def render_markdown(properties, unmapped):
    by_symbolizer = {}
    for prop in properties:
        by_symbolizer.setdefault(prop["symbolizer"], []).append(prop)

    live = [p["cartocss"] for p in properties if p["live"]]
    out = [
        "---",
        "title: CartoCSS property reference",
        'description: "Every property a CartoCSS style may set, its value kind, and whether a param:: driving it stays live"',
        "sidebar_position: 20",
        "---",
        "",
        "# CartoCSS property reference",
        "",
        ":::info Generated",
        "`scripts/gen-cartocss-properties.py` reads the `bindProperty()` calls in",
        "`libs-massif/mapnikvt/src/mapnikvt/*Symbolizer.h` and the name map in",
        "`CartoCSSMapnikTranslator.cpp`. Edit those, then re-run the script — never this page.",
        ":::",
        "",
        f"{len(properties)} properties across {len(by_symbolizer)} symbolizers.",
        "",
        "## Reading the table",
        "",
        "- **live** — the value is re-evaluated every frame, so a `param::` reaching *only* properties",
        "  like these changes the map with a **redraw**. See",
        "  [Style parameters](style-parameters.md).",
        "- **baked** — the property is also read while the tile is built (it sizes a raster, shapes a",
        "  join, decides a glyph). A `param::` reaching one forces a **re-decode of every visible",
        "  tile**, however live its type is.",
        "- Liveness is decided **per parameter across the whole style**, not per use: one baked use",
        "  anywhere makes the parameter baked everywhere.",
        "",
        f"Live-capable properties: {len(live)} of {len(properties)}.",
        "",
    ]

    for symbolizer in sorted(by_symbolizer):
        out += [
            f"## `{symbolizer}`",
            "",
            "| CartoCSS | mapnik | Value | Default | Live | Baked |",
            "|---|---|---|---|---|---|",
        ]
        for prop in by_symbolizer[symbolizer]:
            mapnik = f"`{prop['mapnik']}`" if prop["mapnik"] else "—"
            default = f"`{prop['default']}`" if prop["default"] else ""
            out.append(
                f"| `{prop['cartocss']}` | {mapnik} | {prop['kind']} | {default} "
                f"| {'yes' if prop['live'] else ''} | {'yes' if prop['baked'] else ''} |"
            )
        out.append("")

    if unmapped:
        out += [
            "## Not resolved",
            "",
            "Names the translator maps but this generator could not tie to a bound property — a bug",
            "in either the map or the script, and worth looking at before trusting the table above.",
            "",
            "| CartoCSS | mapnik | Why |",
            "|---|---|---|",
        ]
        out += [f"| `{c}` | `{m}` | {why} |" for c, m, why in unmapped]
        out.append("")

    return "\n".join(out)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true", help="fail if the outputs are out of date")
    args = parser.parse_args()

    classes = parse_symbolizers()
    symbolizer_list, entries = parse_translator()
    properties, unmapped = build(classes, symbolizer_list, entries)

    markdown = render_markdown(properties, unmapped)
    payload = json.dumps(
        {"symbolizers": sorted({p["symbolizer"] for p in properties}), "properties": properties},
        indent=2,
    ) + "\n"

    if args.check:
        stale = [
            path.relative_to(REPO)
            for path, want in ((MARKDOWN_OUT, markdown), (JSON_OUT, payload))
            if not path.exists() or path.read_text(encoding="utf-8") != want
        ]
        if stale:
            print("Out of date, re-run scripts/gen-cartocss-properties.py:", file=sys.stderr)
            for path in stale:
                print(f"  {path}", file=sys.stderr)
            return 1
        print(f"Up to date ({len(properties)} properties).")
        return 0

    JSON_OUT.parent.mkdir(parents=True, exist_ok=True)
    MARKDOWN_OUT.write_text(markdown, encoding="utf-8")
    JSON_OUT.write_text(payload, encoding="utf-8")
    print(f"{len(properties)} properties, {sum(p['live'] for p in properties)} live, "
          f"{sum(p['baked'] for p in properties)} baked, {len(unmapped)} unresolved")
    for cartocss, mapnik, why in unmapped:
        print(f"  unresolved: {cartocss} -> {mapnik} ({why})", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
