/**
 * The spec shapes `massif-api.json` cannot describe.
 *
 * The schema records CONSTRUCTOR signatures, and `all/native/api/SpecFactories.cpp` exists
 * precisely for the shapes no signature says: a geometry read out of GeoJSON, a search service
 * built from the layer it is already showing. A generator reading the schema alone emits the
 * constructor and nothing else, so those keys came back as errors on code the SDK accepts.
 *
 * Everything here is written by hand and has to be KEPT IN STEP WITH SpecFactories.cpp. It is
 * deliberately not the whole file: the routing and geocoding requests are hand-written too, but
 * `create(kind, id, spec, className)` already covers them, so they are left to that overload
 * rather than being restated here.
 *
 * `extra` interfaces join their kind's union under the same `type`, which is how a shape that
 * REPLACES the constructor - rather than adding to it - keeps both sets of required keys honest.
 */
export const FACTORY_SPECS = {
    geometry: [
        {
            name: 'GeometrySpec_geojson',
            /** Not in `schema.kinds`, so `SpecType<'geometry'>` has to be told about it. */
            specType: 'geojson',
            cppClass: 'massif::Geometry',
            doc: [
                'A geometry read out of GeoJSON, which `buildGeometry` handles itself: every other',
                'type has a constructor taking the shape it names, and this one takes a document.'
            ],
            keys: [
                {
                    name: 'type',
                    type: "'geojson'"
                },
                {
                    // `object` alongside `Json`, the way setGeoJSON and addFeature take it: a
                    // GeoJSON document typed as an INTERFACE - @types/geojson's `Polygon` - has no
                    // index signature and so is not assignable to `Json`, however plain it is.
                    name: 'geojson',
                    type: 'Json | string | object',
                    required: true,
                    doc: 'The document, as JSON or as the text of it - `GeoJSONGeometryReader` reads either.'
                },
                {
                    name: 'projection',
                    type: 'ProjectionName',
                    doc: 'What to leave the coordinates in. GeoJSON is lon/lat by definition, so this is only for a consumer working in metres.'
                }
            ]
        }
    ],
    search: [
        {
            // No `specType`: this is a second SHAPE of the `vectortile` type the schema already
            // carries, not a new one, and it builds the same class.
            name: 'SearchSpec_vectortile_layer',
            doc: [
                'A vector tile search over a layer that is already on the map.',
                '',
                'The constructor takes a source and a decoder; this takes the LAYER and reads both off',
                'it, so a search reads exactly what the user is looking at and neither is built twice.'
            ],
            keys: [
                {
                    name: 'type',
                    type: "'vectortile'"
                },
                {
                    name: 'layer',
                    type: 'Handle | string | LayerSpec',
                    required: true,
                    doc: 'The vector tile layer to search - its data source and its tile decoder are taken from it.'
                }
            ],
            /** The constructor spec's own keys that are properties, and so still apply here. */
            inheritOptional: 'SearchSpec_vectortile'
        }
    ]
};
