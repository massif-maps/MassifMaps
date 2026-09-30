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

## The web SDK in a release

`build.yml`'s `build-web` job builds the web SDK next to the Android and iOS ones, from the same
version input - twice, `standard` as `massif-web.*` and `full` as `massif-web-full.*`
([variants](../maintenance/web-build.md#variants)):

- `MassifMaps-web-<version>.zip` on the GitHub release: `dist/web`, the files an app serves.
- npm `@massif-maps/api` (`bindings/js`), then `@massif-maps/web` (`dist/web`, which depends on
  it at the same version), with provenance. Publishing needs the `NPM_TOKEN` secret, which
  `release-style-tools.yml` already uses.

A run with `publish` off keeps the zip as a workflow artefact instead. What the package contains
and how an app hosts it: [the web guide](/docs/getting-started/web).

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
