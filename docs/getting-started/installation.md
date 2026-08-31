---
title: Installation
sidebar_position: 1
---

# Installation

:::tip Version
Always use the latest version from the
[Releases page](https://github.com/massif-maps/MassifMaps/releases). The `5.x` line below is an example.
:::

## Android

Add JitPack and the dependency to your app's `build.gradle`:

```groovy
repositories {
    mavenCentral()
    maven { url 'https://jitpack.io' }
}

dependencies {
    implementation 'com.github.massif-maps:MassifMaps-android-aar:5.0.0'
}
```

Add the INTERNET permission to `AndroidManifest.xml` (needed for online tiles/services):

```xml
<uses-permission android:name="android.permission.INTERNET"/>
```

## iOS

Use Swift Package Manager:

1. In Xcode: **File → Add Packages…**
2. Paste the package URL:

   ```
   https://github.com/massif-maps/MassifMaps-ios-swift
   ```
3. Pick a version and add it to your target.

You can also download a prebuilt framework from the
[Releases page](https://github.com/massif-maps/MassifMaps/releases).

## JavaScript, through an integration

You do not have to write native code. A **framework integration** wraps the same Android and iOS
artifacts, so every renderer feature is reachable from JavaScript.

```bash
npm install @nativescript-community/ui-massifmaps
```

```ts
import { MassifMap } from '@nativescript-community/ui-massifmaps/api';
```

NativeScript is the one that ships today — Vue, Svelte, Angular and plain TypeScript, on Android
and iOS. Flutter and React Native bindings are planned, same shape: a binding over the published
native builds, no second renderer. The full list, versions and status:
**[Integrations](/integrations)**.

Every [example](/examples) carries its NativeScript source beside the Java and Objective-C ones, so
the same map reads the same in all three.

## No license key

The original CARTO SDK required `MapView.registerLicense(...)` at startup. Massif Maps does not:
there is no license server, and the CARTO online services that needed credentials (Maps API,
SQL API, the hosted basemaps and offline package server) are not part of this fork. Bring your
own tile source and style.

## Building from source

The SDK is a large C++ project built per platform. See
[`BUILDING.md`](https://github.com/massif-maps/MassifMaps/blob/master/BUILDING.md) in the repo for the
full toolchain (a SWIG fork and a boost symlink are required). A typical full build takes an hour
or more; for most apps the prebuilt artifacts above are what you want.

## Standalone routing library

If you only need routing (Valhalla) without the map view, the repo also ships a lightweight
**`routing-lib`**. See the [Routing guide](/docs/guides/routing) and the repository README.

## Next step

→ [Show your first map](/docs/getting-started/your-first-map)
