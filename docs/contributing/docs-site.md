---
title: Building the Docs
sidebar_position: 1
slug: /contributing-docs
---

# Building the documentation

## Where the content lives

**All content is in [`docs/`](https://github.com/massif-maps/MassifMaps/tree/master/docs) at the
repo root** — one tree, readable on GitHub and published as-is. `website/` holds only the
[Docusaurus](https://docusaurus.io/) shell (config, theme, React pages) and reads `../docs` through
`path: '../docs'`. There is no second copy and nothing to sync.

```
docs/
├─ intro.md               getting-started/   guides/      features/   ← app developers
├─ api/                   the surface API: index.mdx authored, reference/ GENERATED
├─ tools/                 the massif-style CLI
├─ examples/              examples.json + screenshots, GENERATED, read by /examples
├─ internals/             rendering/ · build-and-size · performance-log
├─ maintenance/           dependency upgrades, platform quirks
├─ contributing/          this page, release workflow
├─ migration.md
└─ _archive/              superseded, EXCLUDED from the site, not maintained

website/                      Docusaurus shell — config, theme, React pages, static assets
```

Two directories are **generated** and must not be hand-edited:

```bash
python3 scripts/gen-api-docs.py    # docs/api/reference/ from docs/api/massif-api.json
python3 scripts/gen-examples.py    # docs/examples/examples.json from the demo apps
```

`massif-api.json` itself comes from `scripts/gen-api-tables.py --schema`, which runs off
`all/modules/*.i` — so a new SDK property reaches the published reference on the next SDK build,
with nothing to write by hand.

Every published page carries front matter (`title`, `description`, `sidebar_position`); the sidebar
is generated from the folder structure plus each folder's `_category_.json`.

## Testing it locally {#local}

Node ≥ 18 (developed on 26). Everything runs from `website/`.

```bash
cd website
npm install
```

### Write in the dev server

```bash
npm start                    # http://localhost:3000/MassifMaps/
npm start -- --port 3333     # when 3000 is taken
```

Hot reload **does watch `../docs`** even though it is outside the site directory — save a file there
and the page updates in about a second. Mermaid diagrams render here too.

### Verify with the production build

```bash
npm run build      # -> website/build
npm run serve      # serve that build, same URL
```

This is what CI runs, and it is the step that actually checks the site. Several things **only** work
here:

| | `npm start` | `npm run build` |
|---|---|---|
| Hot reload of `../docs` | ✅ | — |
| Mermaid diagrams (`.mdx` only) | ✅ | ✅ |
| Relative markdown links (`04-terrain.md`) | ⚠️ warns in the terminal | ⚠️ warns |
| Route links (`/docs/…`) | ❌ **not checked** | ⚠️ warns, listed at the end |
| Local search | ❌ *"The search index is only available when you run docusaurus build!"* | ✅ |
| `/roadmap` GitHub issue fetch | at server start | at build time |

So a page can look finished in the dev server and still ship a dead `/docs/...` link and a search
index that never saw it. **Write in `npm start`, verify in `npm run build`.**

### What to look for in the output

`onBrokenLinks` is `'warn'`, so **a broken link does not fail the build** — reading `[SUCCESS]` is
not enough. Read the tail:

```bash
npm run build 2>&1 | tail -30
```

A good run ends with `[SUCCESS] Generated static files in "build"` and **no**
`Exhaustive list of all broken links found` block. Then `npm run serve` and search for a phrase from
the page you changed, to confirm it made it into the index.

**One block of broken-*anchor* warnings is expected**, and only that one: every
`/examples#<example-id>` link. Docusaurus collects anchors from MDX headings, and `/examples` is a
React page whose sections come from `examples.json` — the ids are in the HTML and the links work,
but the checker cannot see them. Any other broken anchor is real.

## Four things that silently bite

- **Mermaid renders only in `.mdx`.** `markdown.format` is `'detect'`, so a `.md` page is parsed as
  CommonMark and a ```` ```mermaid ```` block is dropped with **no warning at all** — the diagram is
  simply absent. Rename the page to `.mdx` and update the links pointing at it.
- **Quote front-matter values containing a colon**, or the YAML parse aborts the whole build with
  `Error while parsing Markdown front matter`.
- **Stale output after a move or rename** — `npm run clear`, then build again.
- **`docs/_archive/**` is excluded from the site**, so a link to a page there 404s. It is archive on
  purpose; link to the successor page instead.

## Standalone pages and their data {#pages}

The non-doc pages (`/platforms`, `/roadmap`, `/sponsors`, `/community`, `/integrations`) are React
pages under `website/src/pages/`. Their content is **not** in the JSX — each reads a plain data
module in `website/src/data/`, so editing a page usually means editing one array:

| Page | Data file | Edit it to… |
|---|---|---|
| `/platforms` | `src/data/platforms.js` | change a platform's status (`supported` / `planned` / `legacy`) or a row of the feature matrix |
| `/sponsors` | `src/data/sponsors.js` | change tier prices, the contact address, or add a sponsor (logo in `static/img/sponsors/`) |
| `/community` | `src/data/community.js` | change the issue/discussion entry points and the repo list |
| `/integrations` | `src/data/integrations.js` | add a framework plugin |

Backticked spans inside those strings render as `<code>` via `src/components/Ticked.js` — no other
markdown is interpreted.

## Roadmap page (GitHub issues) {#roadmap}

`/roadmap` has no content of its own: `website/plugins/roadmap-issues/` fetches the issues labelled
**`roadmap`** in `massif-maps/MassifMaps` **at build time** and exposes them as plugin global data.
Each card takes the issue title, the first image in the body (markdown `![]()` or a raw `<img>`)
and a teaser of the remaining text.

Columns come from extra labels on the same issue, most-advanced first:

| Issue labels | Column |
|---|---|
| `roadmap` + `status:in-progress` | In progress |
| `roadmap` + `status:next` | Next up |
| `roadmap` alone | Exploring |
| `roadmap`, issue closed | Shipped |

Because the fetch happens at build time, the page only refreshes when the site is rebuilt — the
`schedule:` cron in `docs.yml` rebuilds it nightly. CI passes `GITHUB_TOKEN` to lift the anonymous
60 req/h rate limit; a local `npm run build` works without one. When the API cannot be reached the
build does **not** fail: it falls back to `src/data/roadmap-fallback.json` (refreshed on every
successful build) and the page shows a "may be out of date" banner.

Repo, label and column mapping are plugin options — override them in `docusaurus.config.js`:

```js
['./plugins/roadmap-issues', {owner: 'massif-maps', repo: 'MassifMaps', label: 'roadmap'}],
```

## The guides are authored now

`docs/guides/*.mdx` used to be **generated** from the CARTO Jekyll sources by
`scripts/docs/convert-guides.py`. They are not any more: the four guides that only described CARTO
services were dropped, the rest were rewritten against this SDK, and the converter and its vendored
inputs were deleted with them. Edit the `.mdx` directly.

The [Examples gallery](https://massif-maps.github.io/MassifMaps/examples) is the reference for
runnable code — a guide explains a concept and links the example rather than carrying a fifth copy
of the same snippet in five languages.

## API reference (Javadoc + Jazzy) {#api}

The per-language API reference is generated from the SWIG bindings, then dropped into
`website/static/api/{android,ios}` so it is served under `/api/...`:

```bash
# Android Javadoc  ->  website/static/api/android
scripts/docs/gen-api-android.sh

# iOS Jazzy        ->  website/static/api/ios
scripts/docs/gen-api-ios.sh
```

Both scripts first run the SWIG proxy generators (`swigpp-java.py` / `swigpp-objc.py`) to produce
the language sources, then run `javadoc` / `jazzy`. They need the SWIG fork and (for iOS) a macOS
host with `jazzy` installed — the CI workflow sets these up automatically. See each script's header
for prerequisites.

## Screenshots & videos {#screenshots}

Feature pages reference images under `website/static/img/features/`. The terrain / hillshade /
contour shots and the pan video there were captured from the `scripts/android-dev` demo:

```bash
# Boot an emulator / connect a device, then:
scripts/docs/capture-screenshots.sh terrain-hero      # a still
RECORD=1 scripts/docs/capture-screenshots.sh terrain  # still + ~14s video
```

The demo streams its terrain data from public online tiles (a terrarium DEM +
an OpenFreeMap vector basemap), so the emulator only needs internet — no map data is pushed to
the device. The native libraries are prebuilt under `scripts/android-dev/massif/`, so the
app builds in seconds. The script builds (`assembleDebug --offline`), installs, launches, grabs a
screenshot (and optionally a screen recording), then uses `ffmpeg` to crop the Android status/nav
bars and encode a web-friendly JPEG/MP4. Drop the results into `website/static/img/features/`.

For distinct shots (top-down hillshade, close-up contours, a low-angle 3D view), edit the demo's
`SecondFragment` camera (`setFocusPos` / `setZoom` / `setTilt` — Massif tilt is `90` = top-down,
low = horizon) and comment out `addTerrainControls` to hide the debug UI, then restore it.

## Deployment

A GitHub Actions workflow
([`.github/workflows/docs.yml`](https://github.com/massif-maps/MassifMaps/blob/master/.github/workflows/docs.yml))
builds the site + API reference and deploys to GitHub Pages on every push to `master` and on every
published release. See [Release workflow](/docs/release-workflow).
