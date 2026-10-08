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

## The npm packages in an SDK release

Testing what a run published, channel by channel: [Testing a release](release-testing.md).

`build.yml` builds the web SDK and the style tools next to Android and iOS, from the same version:

- `build-web`: the web module twice, `standard` as `massif-web.*` and `full` as `massif-web-full.*`
  ([variants](../maintenance/web-build.md#variants)); `MassifMaps-web-<version>.zip` on the GitHub
  release (`dist/web`, the files an app serves), and the `@massif-maps/api` and `@massif-maps/web`
  tarballs.
- `style-tools` calls `release-style-tools.yml`: the style compiler wasm and the
  `@massif-maps/style-tools` tarball.
- `update-release`, once every platform has built: attaches the wasm, writes the notes and makes
  the release public.
- `publish-npm`, after that: publishes the three tarballs with `scripts/npm-packages.py publish`,
  `@massif-maps/api` first, with provenance, skipping a version npm already has.
  npm trusts the workflow itself (trusted publishing, set per package on npmjs.com), so there is
  no npm token.

With `prerelease` on, the GitHub release is a prerelease and npm gets the `next` dist-tag instead
of `latest`. A run with `publish` off keeps the zip and the tarballs as workflow artefacts. The same
script packs the tarballs locally: [BUILDING.md](https://github.com/massif-maps/MassifMaps/blob/master/BUILDING.md#npm-packages).
What the web package contains and how an app hosts it: [the web guide](/docs/getting-started/web).

`@massif-maps/style-tools` also has dev builds: each push to master touching the style compiler
(`libs-massif`, `tools/style-cli`) publishes `<latest + 1 patch>-dev.<run>` - `6.1.3-dev.412` after
6.1.2 - under the `dev` dist-tag, from `release-style-tools.yml` (`publish-dev`). It sorts above
every release so far and below the next, so `@massif-maps/style-tools@dev` is always the newest
compiler and a `^6.1.2` range never picks it. npmjs.com lists that workflow as a second trusted
publisher of the package.

## Release notes

`scripts/release-notes.py` writes the notes of both releases and the SDK's `CHANGELOG.md` entry:
one line per `feat`/`fix` squash commit since the last final release, its PR title verbatim. A
commit goes to a release by the paths it touches:

| Touches | SDK notes | Styles notes |
|---|---|---|
| `styles/massif`, `tools/style-sprite`, `tools/icon-font` only | — | yes |
| anything else only | yes | — |
| both | yes | yes |

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
