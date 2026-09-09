# Fonts this style carries

A style's own fonts are registered ahead of the system ones (`MBVectorTileDecoder`, "Register the
fonts of the style first"), and they are the ONLY way a face reaches a build with no system fonts —
the web one. `massif-style mapbox2css --fonts fonts` copies them into the generated project and
names them in its `project.json`; the decoder itself needs no list, it scans `<style>/fonts/`.

| File | Face | Licence |
|---|---|---|
| `NotoSans-Bold.ttf` | Noto Sans Bold, subset to printable ASCII | SIL Open Font License 1.1 |

**Subset, not the whole face.** A road shield draws a `ref` — digits, letters, a space, the odd
suffix (`D 512a`) — so the face is cut to `U+0020-U+007E`: 132 glyphs, 14 KB against 569. Cut with

```sh
pyftsubset NotoSans-Bold.ttf --unicodes="0020-007E" --name-IDs="*" --output-file=fonts/NotoSans-Bold.ttf
```

`--name-IDs="*"` is load-bearing: the decoder resolves a face by the name in its own table, so a
subset that drops the name table stops answering to `Noto Sans Bold` and every shield falls back.

Source: the full face vendored at `libs-external/harfbuzz/harfbuzz/test/api/fonts/NotoSans-Bold.ttf`.
Noto is © Google, under the SIL Open Font License 1.1 (<https://openfontlicense.org>).

Keep this directory to the faces the style NAMES. Everything here is carried by every app that
ships the style, and the web preview fetches it on load.
