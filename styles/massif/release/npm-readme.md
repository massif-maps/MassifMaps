# @massif-maps/styles

The Massif map styles — **streets**, **outdoor**, **topo**, **hybrid** and **e-ink** — on
[OpenMapTiles](https://openmaptiles.org/schema/) vector tiles, for MapLibre / Mapbox GL and for the
[Massif Maps SDK](https://github.com/massif-maps/MassifMaps).

| Folder | What it is | For |
|---|---|---|
| `maplibre/` | `<variant>.json` + `sprite/` | MapLibre GL JS / Native, Mapbox GL |
| `cartocss/` | CartoCSS project: `project.json`, `<variant>.json`, `.mss`, icons, `legend.json` | the SDK, style parameters live |
| `cartocss-compiled/` | the same compiled to mapnik XML, `<variant>.xml` | the SDK, no CartoCSS parse at startup |
| `cartocss-iconfont/` | CartoCSS project, POI icons drawn from the bundled `MassifIcons` font | the SDK, one font instead of PNGs |
| `cartocss-iconfont-compiled/` | that compiled | |

The MapLibre styles read OpenFreeMap's tiles and Mapterhorn's DEM. The optional archives
(`bathymap`, `contours`, `routes`, `satellite`) are empty placeholders you replace with your own.

Documentation — which flavour to pick, loading it, style parameters, adding the optional sources,
extending the style: https://massif-maps.github.io/MassifMaps/docs/styles/massif

Map data © OpenStreetMap contributors. POI icons: Maki (CC0) and openstreetmap-carto (CC0).
The style is MIT-0: use it for anything, no notice required.
