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

`build.yml` builds the web SDK and the style tools next to Android and iOS, from the same version:

- `build-web`: `MassifMaps-web-<version>.zip` on the GitHub release (`dist/web`, the files an app
  serves), and the `@massif-maps/api` and `@massif-maps/web` tarballs.
- `style-tools` calls `release-style-tools.yml`: the style compiler wasm and the
  `@massif-maps/style-tools` tarball.
- `publish-npm`, once every platform has built: attaches the wasm to the release and publishes the
  three tarballs with `scripts/npm-packages.py publish`, `@massif-maps/api` first, with provenance.
  It needs the `NPM_TOKEN` secret.

With `prerelease` on, the GitHub release is a prerelease and npm gets the `next` dist-tag instead
of `latest`. A run with `publish` off keeps the zip and the tarballs as workflow artefacts. The same
script packs the tarballs locally: [BUILDING.md](https://github.com/massif-maps/MassifMaps/blob/master/BUILDING.md#npm-packages).
What the web package contains and how an app hosts it: [the web guide](/docs/getting-started/web).

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
