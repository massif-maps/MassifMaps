---
title: Release Workflow
sidebar_position: 2
slug: /release-workflow
---

# Release & auto-publishing

The docs (and API reference) publish themselves. A GitHub Actions workflow rebuilds and deploys to
GitHub Pages:

- on every **push to `master`** that touches `docs/`, `website/`, `scripts/docs/` or the workflow itself,
- on every **published GitHub Release**,
- **nightly** (so `/roadmap` picks up issue changes),
- and **manually** via *Run workflow* (workflow_dispatch).

## What the workflow does

1. Checks out the repo **with submodules** (`libs-external`; `libs-massif` is in-tree).
2. Generates the **Android Javadoc** and **iOS Jazzy** reference from the SWIG bindings into
   `website/static/api/{android,ios}`.
3. Downloads the newest `web-site` artefact of
   [`web-preview.yml`](https://github.com/massif-maps/MassifMaps/blob/master/.github/workflows/web-preview.yml)
   into `website/static`: the module the `/preview` page runs and the package the live examples
   load (`static/massif`). That workflow builds on every master push touching the SDK or `web/`,
   and triggers this one when it is done.
4. Builds the **Docusaurus** site (`npm ci && npm run build`).
5. Uploads the result and **deploys to GitHub Pages**.

The workflow file is
[`.github/workflows/docs.yml`](https://github.com/massif-maps/MassifMaps/blob/master/.github/workflows/docs.yml).

## Cutting a release

**Actions → Release → Run workflow** starts every release: the SDK, the Massif styles and the style
tools. Each is its own semver with its own tag, so any of them can ship alone.

| Package | Tag | Published as |
|---|---|---|
| SDK (Android, iOS, web) | `v<version>` | GitHub release, JitPack, Swift package, npm `@massif-maps/api` and `web` |
| Massif styles | `massif-styles-v<version>` | GitHub release, npm `@massif-maps/styles`, the website |
| Style tools | `style-tools-v<version>` | GitHub release (the wasm), npm `@massif-maps/style-tools` |

You say what each should bump, never what its number is. `sdk`, `styles` and `tools` take `none` (the
default: leave it out), `auto`, `patch`, `minor`, `major`, as dropdowns:

```sh
gh workflow run release.yml --repo massif-maps/MassifMaps -f styles=patch -f publish=true
gh workflow run release.yml --repo massif-maps/MassifMaps -f sdk=minor -f styles=auto -f tools=auto -f publish=true
gh workflow run release.yml --repo massif-maps/MassifMaps -f sdk=minor -f prerelease=true -f publish=true
```

- **`auto`** reads the conventional commits since the package's last release, routed to it by the
  paths they touch ([release notes](#release-notes)): a breaking one is a major, a `feat` a minor,
  anything else a patch, and no commit at all releases nothing. `auto` on all three is "release what
  changed".
- **`prerelease`** makes release candidates: `6.3.0-rc.1`, the number counted from the existing tags,
  npm dist-tag `next`. The same run without it releases `6.3.0`.
- **`publish` off** is a dry run: the plan, then every build, and no tag, release or upload.
- The first job, `plan`, prints the table (last release, commits since, what `auto` would pick, the next
  version) in the run summary and stops on a conflict — a tag that exists, a version below one that
  does — before anything is built. `python3 scripts/release-version.py styles auto` prints the same
  from a checkout.
- A run releases one commit: every package it names is tagged at the same SHA.

The styles declare the SDK they need (`styles/massif/compat.json`,
[Massif style release](massif-style-release.md#the-sdk-a-release-needs)). Styles that need an SDK not
yet released fail `plan` unless the same run releases the SDK.

The called workflows (`build.yml`, `release-styles.yml`, `release-style-tools.yml`) are not started
by hand any more, except the style tools' dev builds below. npmjs.com trusts the workflow that
*dispatches* the run, so each package lists `release.yml` as its trusted publisher.

## The npm packages in an SDK release

Testing what a run published, channel by channel: [Testing a release](release-testing.md).

`build.yml` builds the web SDK next to Android and iOS, from the SDK version:

- `build-web`: the web module twice, `standard` as `massif-web.*` and `full` as `massif-web-full.*`
  ([variants](../maintenance/web-build.md#variants)); `MassifMaps-web-<version>.zip` on the GitHub
  release (`dist/web`, the files an app serves), and the `@massif-maps/api` and `@massif-maps/web`
  tarballs.
- `update-release`, once every platform has built: writes the notes and makes the release public.
- `publish-npm`, after that: publishes the two tarballs with `scripts/npm-packages.py publish`,
  `@massif-maps/api` first, with provenance, skipping a version npm already has.
  npm trusts the workflow itself (trusted publishing, set per package on npmjs.com), so there is
  no npm token.

With `prerelease` on, the GitHub release is a prerelease and npm gets the `next` dist-tag instead
of `latest`. A run with `publish` off keeps the zip and the tarballs as workflow artefacts. The same
script packs the tarballs locally: [BUILDING.md](https://github.com/massif-maps/MassifMaps/blob/master/BUILDING.md#npm-packages).
What the web package contains and how an app hosts it: [the web guide](/docs/getting-started/web).

`@massif-maps/style-tools` is released on its own (`release-style-tools.yml`, called with `tools=…`: a
`style-tools-v<version>` release carrying the compiler wasm, then npm). It also has dev builds: each push to master touching the style compiler
(`libs-massif`, `tools/style-cli`) publishes `<latest + 1 patch>-dev.<run>` - `6.1.3-dev.412` after
6.1.2 - under the `dev` dist-tag, from `release-style-tools.yml` (`publish-dev`). It sorts above
every release so far and below the next, so `@massif-maps/style-tools@dev` is always the newest
compiler and a `^6.1.2` range never picks it. npmjs.com lists that workflow as a second trusted
publisher of the package.

## Release notes

`scripts/release-notes.py` writes the notes of the three releases and the SDK's `CHANGELOG.md` entry:
one line per `feat`/`fix` squash commit since the package's last final release, its PR title
verbatim. A commit goes to a release by the paths it touches, and the same routing feeds `auto`:

| Touches | SDK | Styles | Tools |
|---|---|---|---|
| `styles/massif`, `tools/style-sprite`, `tools/icon-font` | — | yes | — |
| `tools/style-cli` | — | — | yes |
| `libs-massif/cartocss` (the compiler the wasm is built from), `scripts/gen-cartocss-properties.py` | yes | — | yes |
| anything else | yes | — | — |

A commit touching several rows lands in each.

`build.yml` writes the SDK notes into the draft release before the first build and prints them in
the run summary. Edit the draft body on GitHub while the builds run: the final job copies it (minus
the installation section) into `CHANGELOG.md` and publishes it as is.

`docs`, `website`, `tests`, `.claude`, `.github` and `tools/style-preview` count for neither, so a
styles PR with its doc page stays out of the SDK's notes. A PR touching both lands in both under
one title, so the SDK change goes in its own PR, titled for the SDK.

## The Massif styles

`release-styles.yml` releases `styles/massif` on its own version (`massif-styles-v<version>`): npm
`@massif-maps/styles`, one zip per flavour, and a redeploy of this site, which serves the MapLibre
styles under `/styles/massif/`. See [Massif style release](massif-style-release.md).

## One-time setup

Enable Pages for the repository:

1. **Settings → Pages → Build and deployment → Source: GitHub Actions.**
2. Push to `master` (or run the workflow manually). The site publishes to
   `https://massif-maps.github.io/MassifMaps/`.

## Versioned docs (optional)

Docusaurus supports [versioned docs](https://docusaurus.io/docs/versioning). To snapshot the
current docs for a release tag:

```bash
cd website
npm run docusaurus docs:version 5.0.0
```

This freezes `docs/` into `versioned_docs/version-5.0.0/` and adds a version dropdown. Commit the
snapshot; the workflow will publish all versions. Do this per major/minor release you want to keep
browsable.
