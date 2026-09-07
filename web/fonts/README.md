# Fonts the web build ships

One text face, preloaded into the wasm module's virtual filesystem at `/fonts` and used as the
fallback for any face a style names that the build does not carry. Without it the module renders
geometry and **no labels at all** — which is what the CI build did before this directory existed,
since `web/demo/fonts/` is gitignored and only ever held fonts dropped in by hand.

| File | Face | Licence |
|---|---|---|
| `Roboto.ttf` | Roboto Regular, version 2.138 | Apache License 2.0 |

Roboto is © 2011 Google Inc., licensed under the Apache License 2.0
(<http://www.apache.org/licenses/LICENSE-2.0>) — the licence and its URL are recorded in the font's
own `name` table.

This file is preloaded along with the fonts and shows up at `/fonts/README.md` inside the module -
700 bytes, kept because provenance belongs next to a vendored binary. `SystemFontUtils` only ever
looks for `<name>.ttf`, `.otf` or `.ttc`, so it is inert.

Keep this to the minimum a preview needs. Every file here is preloaded, so it is paid for on every
page load; `web/demo/fonts/` is the place for extra faces while developing, and is not tracked.
