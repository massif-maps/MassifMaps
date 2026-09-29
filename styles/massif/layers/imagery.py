"""The hybrid variant's ground: an app-supplied raster (`satellite`), drawn under everything else.
CartoCSS cannot draw a raster, so on the SDK it is the app's RasterTileLayer below the vector one."""


def layers(v):
    return [{'id': 'satellite', 'type': 'raster', 'source': 'satellite',
             'paint': {'raster-opacity': 1},
             'metadata': {'massif:sdk-layer': {'type': 'raster', 'below': True}}}]
