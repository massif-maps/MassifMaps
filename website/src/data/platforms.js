/**
 * Platform support matrix — single source of truth for /platforms and the homepage strip.
 *
 * status: 'supported' | 'planned' | 'legacy'
 *   supported — built and released by CI, prebuilt artifacts published
 *   planned   — on the roadmap, not usable yet
 *   legacy    — code is in the tree but unmaintained and untested by CI
 */

export const Platforms = [
  {
    id: 'android',
    name: 'Android',
    icon: '🤖',
    status: 'supported',
    languages: ['Java', 'Kotlin'],
    distribution: 'JitPack — `com.github.massif-maps:MassifMaps`',
    minVersion: 'Android 5.0 (API 21)+, OpenGL ES 2.0',
    docs: '/docs/getting-started/installation#android',
    api: 'pathname:///MassifMaps/api/android/',
    note: 'Reference platform. The demo app under `scripts/android-dev` is the main development loop.',
  },
  {
    id: 'ios',
    name: 'iOS',
    icon: '📱',
    status: 'supported',
    languages: ['Swift', 'Objective-C'],
    distribution: 'Swift Package Manager — `massif-maps/MassifMaps-ios-swift`',
    minVersion: 'iOS 13+, arm64, Metal via ANGLE',
    docs: '/docs/getting-started/installation#ios',
    api: 'pathname:///MassifMaps/api/ios/',
    note: 'Static framework, GLES2 translated to Metal through ANGLE.',
  },
  {
    id: 'nativescript',
    name: 'NativeScript',
    icon: '📜',
    status: 'supported',
    languages: ['TypeScript', 'JavaScript', 'Svelte', 'Vue'],
    distribution: 'npm — `@nativescript-community/ui-massifmaps`',
    minVersion: 'Wraps the Android + iOS builds',
    // The plugin lives in its own repo — our installation page is native-only.
    docs: 'https://github.com/nativescript-community/ui-massifmaps',
    note: 'A binding over the published Android and iOS artifacts, built on the facade API. Typed events and layer specs from TypeScript.',
  },
  {
    id: 'desktop',
    name: 'Desktop',
    icon: '🖥️',
    status: 'planned',
    languages: ['C++'],
    distribution: 'Not published yet',
    minVersion: 'macOS / Linux / Windows via ANGLE',
    note: 'The generic pieces already exist — a Pion-based HTTP client and only ~8 platform-specific files per target. What is missing is the packaging and a windowing host.',
  },
  {
    id: 'web',
    name: 'Web',
    icon: '🌐',
    status: 'planned',
    languages: ['JavaScript', 'TypeScript'],
    distribution: 'Not published yet',
    minVersion: 'WebAssembly + WebGL 1',
    note: 'The renderer is GLES2, so WebGL 1 is a direct target. The real blocker is threading: the tile pool, the cull worker and the label-placement worker all assume real threads.',
  },
  {
    id: 'flutter',
    name: 'Flutter',
    icon: '🐦',
    status: 'planned',
    languages: ['Dart'],
    distribution: 'Not published yet',
    minVersion: 'Wraps the Android + iOS builds',
    note: 'A binding, not a port — it rides on the platform views and the existing native artifacts.',
  },
  {
    id: 'react-native',
    name: 'React Native',
    icon: '⚛️',
    status: 'planned',
    languages: ['JavaScript', 'TypeScript'],
    distribution: 'Not published yet',
    minVersion: 'Wraps the Android + iOS builds',
    note: 'Same shape as Flutter: a turbo-module over the published Android and iOS artifacts.',
  },
  {
    id: 'uwp',
    name: 'UWP / Windows',
    icon: '🪟',
    status: 'legacy',
    languages: ['C#'],
    distribution: 'Build from source only',
    minVersion: 'Windows 10 UWP',
    note: 'Inherited from the original CARTO SDK. The sources and `build-winphone.py` are still in the tree but nothing builds them in CI — treat as unverified.',
  },
  {
    id: 'xamarin',
    name: '.NET / Xamarin',
    icon: '🔷',
    status: 'legacy',
    languages: ['C#'],
    distribution: 'Build from source only',
    minVersion: 'Xamarin.Android / Xamarin.iOS',
    note: 'Inherited from the original CARTO SDK. `build-xamarin.py` and the SWIG C# generator are still there, but untested against the fork.',
  },
];

export const StatusLabels = {
  supported: {label: 'Supported', className: 'statusSupported'},
  planned: {label: 'Coming', className: 'statusPlanned'},
  legacy: {label: 'Unmaintained', className: 'statusLegacy'},
};

/**
 * Capability matrix. Values keyed by platform id: true | false | 'unverified'
 * ('unverified' = the shared C++ core has it, but no one has built or run that
 * binding against this fork). Planned platforms have nothing to compare yet.
 */
export const CapabilityColumns = ['android', 'ios', 'uwp', 'xamarin'];

export const Capabilities = [
  {
    name: 'Vector tiles (MVT, MLT) + CartoCSS',
    to: '/docs/features/maplibre-tiles',
    values: {android: true, ios: true, uwp: 'unverified', xamarin: 'unverified'},
  },
  {
    name: 'Raster tiles, MBTiles, PMTiles',
    to: '/docs/features/pmtiles',
    values: {android: true, ios: true, uwp: 'unverified', xamarin: 'unverified'},
  },
  {
    name: '3D terrain + draping',
    to: '/docs/features/3d-terrain',
    values: {android: true, ios: true, uwp: 'unverified', xamarin: 'unverified'},
  },
  {
    name: 'Hillshade & on-the-fly contours',
    to: '/docs/features/hillshade',
    values: {android: true, ios: true, uwp: 'unverified', xamarin: 'unverified'},
  },
  {
    name: 'Sky, sun lighting & shadows',
    to: '/docs/features/sky-sun-shadows',
    values: {android: true, ios: true, uwp: 'unverified', xamarin: 'unverified'},
  },
  {
    name: 'Post-processing shaders',
    to: '/docs/features/post-processing',
    values: {android: true, ios: true, uwp: 'unverified', xamarin: 'unverified'},
  },
  {
    name: 'Valhalla street routing',
    to: '/docs/guides/routing',
    values: {android: true, ios: true, uwp: 'unverified', xamarin: 'unverified'},
  },
  {
    name: 'Offline geocoding',
    to: '/docs/guides/geocoding',
    values: {android: true, ios: true, uwp: 'unverified', xamarin: 'unverified'},
  },
  {
    name: 'Offline packages (Package Manager)',
    to: '/docs/guides/package-manager',
    values: {android: true, ios: true, uwp: 'unverified', xamarin: 'unverified'},
  },
  {
    name: 'Prebuilt artifacts published',
    to: '/docs/getting-started/installation',
    values: {android: true, ios: true, uwp: false, xamarin: false},
  },
  {
    name: 'Covered by CI',
    values: {android: true, ios: true, uwp: false, xamarin: false},
  },
];
