# Writing documentation in mobile-sdk

Pick the home first — most changes need **none** of them. The full routing table and the bar for
each kind of page are in [`.claude/CLAUDE.md`](../../../CLAUDE.md#documentation--every-change-updates-it);
this page is the how-to-write guidance behind it.

| What you learned                                  | Where it goes                                           |
| ------------------------------------------------- | ------------------------------------------------------- |
| Why this code is weird / non-obvious              | a comment next to the code                              |
| What a public API does and how an app calls it    | doc comment in the SWIG `.i` (it becomes Javadoc/Jazzy) |
| A debugging technique or invariant an agent needs | the relevant section of the root `CLAUDE.md`            |
| How a subsystem works, for the next maintainer    | `docs/internals/rendering/<subsystem>.md`               |
| A procedure that had to be rediscovered           | `docs/maintenance/<topic>.md`                           |
| A user-facing feature, guide, or config reference | `docs/features/`, `docs/guides/`                        |

## Code comments — WHY only

The default is **no comment**. Write one only when the code cannot carry the information itself:

- A magic constant that came from measurement (`// 12 clip units: chord error of the terrain tesselation at z11`).
- A workaround for platform/driver behaviour (`// glGetUniformLocation, not Shader::getUniformLoc — the latter returns 0 for a dropped uniform and 0 is a valid location`).
- An invariant a future edit would silently break (`// prefer the geometry copy with the same (tileId, localId): re-snapping by list order breaks line fitting`).
- An ordering or threading constraint (which thread runs this, what must not clear the culler grid).

Never write: what the next line does, a restated function name, a changelog entry, a TODO with no owner, or a comment that will drift out of date the moment the code moves.

Match the surrounding density: `all/native/` and `libs-massif/vt/` are sparsely commented. A block of prose in a hot render path is noise; one sharp sentence above the constant is the norm.

## Public API — SWIG `.i` doc comments

`all/modules/*.i` is the public surface, and its comments are what app developers actually read: the generated Javadoc/Jazzy under `website/static/api/{android,ios}` comes from there (`scripts/docs/gen-api-android.sh` / `gen-api-ios.sh`).

- Document every new public method/class you add there: one-line purpose, each parameter's unit and range, the default, and what happens at the edges (0, negative, unset).
- Units matter more than prose in this SDK — meters vs tiles vs NDC vs degrees vs hours. State them.
- Say when a setter takes effect (next frame / next tile fetch / requires a re-render) and whether it is thread-safe to call from the app thread.
- A doc-comment-only change still needs the wrappers regenerated (`swigpp-java.py`) before it reaches the site.

## Subsystem knowledge — root `CLAUDE.md`

The root `CLAUDE.md` is the maintainer-facing map: repository layout, the demo-app loop, the renderer debugging playbook, the label pipeline invariants, terrain/fog/sky wiring. Add there when you learned something **durable** that would otherwise cost the next person a day:

- A new invariant, with the failure it prevents ("`LabelCuller::process` must NOT clear the grid — layers must collide").
- A debugging technique that actually worked, and what it distinguished ("A/B per screen row band separated 'tile never loaded' from 'tile drawn but depth-rejected'").
- A trap with a misleading symptom (camera clearance clamp auto-zooming out looks like a broken renderer).

Keep it additive and terse, in the existing voice. A measurement log goes in `docs/internals/performance-log.md`; a superseded plan goes to `docs/_archive/`.

## Pick the home first

| What you learned | Where it goes |
|---|---|
| why this code is weird / non-obvious | one-line comment next to the code |
| what a public API does, its unit, default, when it takes effect | doc comment in `all/modules/*.i` |
| an app-facing capability | `docs/features/<feature>.md` |
| how a subsystem works, for the next maintainer | `docs/internals/rendering/<subsystem>.md` |
| how the *whole* thing fits together | `docs/internals/index.mdx` |
| a number that came from a bench | `docs/internals/performance-log.md`, with camera and method |
| a procedure that had to be rediscovered | `docs/maintenance/<topic>.md` |
| a rename, a removal, a breaking change | `docs/migration.md` |
| a design that was tried and dropped | move the page to `docs/_archive/`, never leave two live versions |
| a debugging technique or invariant an agent needs | root `CLAUDE.md` |
| a behaviour difference from tangram or maplibre | `docs/internals/rendering/11-tangram-diff.md`, plus whichever page also changed |

`docs/` is **one tree**: the source of truth, browsable on GitHub, and published verbatim at
<https://massif-maps.github.io/MassifMaps/> (Docusaurus reads `path: '../docs'`). There is no second
copy to sync. `docs/_archive/**` is excluded from the site and is not maintained — never cite it as
current.

## Technical docs — `docs/internals/`, `docs/maintenance/`

One page per subsystem, scope stated at the top, cross-links instead of repetition — a reader must be able to open one page and stop.

- **Answer four things**: how it works, how it compares to tangram/maplibre, why the choice was made, and what a maintainer needs to change it safely. Nothing else.
- **Record the dead ends.** What was ruled out and what the failure looked like is the expensive part; the fix alone is not enough.
- **End with what is still open** — a "What could be better" section, not a TODO in the code.
- **Numbers carry their method**: the camera, the device, the build flavour. A number without one is not evidence.
- Update the routing table in `docs/internals/index.mdx` (and `rendering/index.mdx`) when adding a page — that table is how the right file gets found without reading the set.

## User-facing docs — `docs/features/`, `docs/guides/`, `docs/getting-started/`

- Show the app-facing call, not the C++ internals: the option, its default, its unit, and a short snippet. Link to the internals page for the mechanism.
- Screenshots come from the demo app via `scripts/docs/capture-screenshots.sh` into `website/static/img/features/` — reference existing images rather than inventing paths.
- `docs/guides/*.mdx` are **authored**. They used to be converted from the legacy CARTO sources; the converter and its inputs are gone, and the CARTO-service-only guides with them.
- **A feature page shows the surface API too.** The object-API snippet is not enough — add the `Spec`/property-path form, and link the generated [`docs/api/reference/`](../../../../docs/api/reference/) page for the full key list. When the class has no spec factory, say so and show `adopt` rather than inventing a `type`.
- `docs/api/reference/` and `docs/examples/examples.json` are **generated** (`scripts/gen-api-docs.py`, `scripts/gen-examples.py`). Never hand-edit either; fix the generator or the `.i` declaration.
- Screenshots in `docs/` are height-capped by CSS (`24rem`); an image alone in its paragraph is framed and centred automatically, so no `<figure>` wrapper is needed for that alone.

## Mechanics that fail the build

- Front matter on every published page: `title`, `description`, `sidebar_position`. **Quote any value containing a colon.**
- Mermaid renders only in `.mdx` — `markdown.format: 'detect'` parses `.md` as CommonMark and silently drops the diagram.
- Relative file links (`04-terrain.md`) between docs; full GitHub URLs for source files.
- `cd website && npm run build` is the only real check: the dev server skips route links and has no search index. `onBrokenLinks` is `'warn'`, so `[SUCCESS]` alone proves nothing — the run is clean only when no `Exhaustive list of all broken links found` block follows it. See [`website/README.md`](../../../../website/README.md).

## What NOT to document

- Anything the code, the git history, or a test already states.
- Speculative future work beyond a named "what could be better" item (that is an issue, not a doc).
- A summary of the change you just made — the commit message and PR body carry that.
