# Internal dependencies for Carto Mobile SDK

This repository contains various libraries that are used by CARTO Mobile SDK.
These libraries have no dependencies with the rest of the SDK, so they
are kept as a separate project.

## Libraries

The libraries included in the project are:

##### CartoCSS
An extended CartoCSS parser and translator to MapnikVT.

##### MapnikVT
A mid level vector tile rendering library that uses Mapnik-like XML style definition language. Includes decoders for MapBox vector tiles and Torque tiles.

#####  VT
A low level vector tile rendering library using OpenGLES 2.

##### NML
Library for loading and rendering 3D models converted from Collada DAE files. Uses OpenGLES 2 for rendering.

##### OSRM
A mobile-friendly routing library that uses routing graphs converted from OSRM data and provides optimized routing using Contraction Hierarchies.

##### SGRE
Simple GeoJSON Routing Engine - a routing engine designed for indoor scenarios, using GeoJSON input files for routing graphs. Works with all kinds of geometry (2D, 3D, lines, polygons).

##### Geocoding
A mobile-friendly geocoding (and reverse geocoding) library that uses special sqlite geocoding database converted from Pelias importer data. Optimized for fast searching with relatively small footprint.

##### MBVTBuilder
A basic library for building MapBox vector tiles from GeoJSON features.


## License
These libraries are licensed under the BSD 3-clause "New" or "Revised" License - see the [LICENSE file](LICENSE) for details.
