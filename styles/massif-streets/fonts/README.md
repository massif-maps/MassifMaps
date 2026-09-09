# Fonts this style carries

A style's own fonts are registered ahead of the system ones (`MBVectorTileDecoder`, "Register the
fonts of the style first"), and they are the ONLY way a face reaches a build with no system fonts —
the web one. `massif-style mapbox2css --fonts fonts` copies them into the generated project and
names them in its `project.json`; the decoder itself needs no list, it scans `<style>/fonts/`.

| File | Face | Licence |
|---|---|---|
| `NotoSans-Bold.ttf` | Noto Sans Bold, subset to printable ASCII | SIL Open Font License 1.1 |
| `NotoSans-Italic.ttf` | Noto Sans Italic, subset to Latin | SIL Open Font License 1.1 |

**Subset, not the whole face.** A road shield draws a `ref` — digits, letters, a space, the odd
suffix (`D 512a`) — so the face is cut to `U+0020-U+007E`: 132 glyphs, 14 KB against 569. Cut with

```sh
pyftsubset NotoSans-Bold.ttf --unicodes="0020-007E" --name-IDs="*" --output-file=fonts/NotoSans-Bold.ttf
```

The italic draws NAMES rather than refs, so ASCII is not enough — `Collège`, `Château`, `Saint-Égrève`
all need Latin-1, and Central European names need Latin Extended-A. It is cut wider to match, at
393 glyphs and 31 KB. Noto Sans ships italic only as a VARIABLE font, so it is pinned to the regular
weight first:

```sh
python3 -c "from fontTools.ttLib import TTFont; from fontTools.varLib import instancer; \
  f = TTFont('NotoSans-Italic[wdth,wght].ttf'); \
  instancer.instantiateVariableFont(f, {'wght': 400, 'wdth': 100}, inplace=True); \
  f.save('NotoSans-Italic-static.ttf')"
pyftsubset NotoSans-Italic-static.ttf --unicodes="0020-007E,00A0-00FF,0100-017F,2013-2014,2018-201D" \
  --name-IDs="*" --output-file=fonts/NotoSans-Italic.ttf
```

`--name-IDs="*"` is load-bearing: the decoder resolves a face by the name in its own table, so a
subset that drops the name table stops answering to `Noto Sans Bold` and every shield falls back.

Sources: the bold from the full face vendored at
`libs-external/harfbuzz/harfbuzz/test/api/fonts/NotoSans-Bold.ttf`, the italic from
`ofl/notosans/NotoSans-Italic[wdth,wght].ttf` in google/fonts — the one vendored beside the bold is
a four-glyph harfbuzz test stub, not a usable face. Noto is © Google, under the SIL Open Font
License 1.1 (<https://openfontlicense.org>).

Keep this directory to the faces the style NAMES. Everything here is carried by every app that
ships the style, and the web preview fetches it on load. The italic is the one face here ahead of
its use: no layer names it yet, and it is carried so that the POI labels this style still owes have
a face to land on instead of falling back to the regular one, the way the shields did before the
bold shipped.
