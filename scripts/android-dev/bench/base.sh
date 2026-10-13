# The map every bench scripts draws by default: the Massif style over the local Rhone-Alpes PMTiles,
# buildings as extrusions - a busy, realistic scene. BENCH_BASE overrides it (BENCH_BASE="--es base composite --es style assets"
# for the old osm CartoCSS one). Needs rhones_alpes/rhone-alpes.pmtiles under /sdcard/alpimaps_mbtiles.
BENCH_BASE="${BENCH_BASE:---es base plain --es style assetzip --es styleAssetZip massif --es vectorFile rhones_alpes/rhone-alpes.pmtiles --es buildings 2}"
