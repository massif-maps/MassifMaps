# Internal dependencies for Carto Mobile SDK

This repository contains various libraries that are used by CARTO Mobile SDK.
These libraries have no dependencies with the rest of the SDK, so they
are kept as a separate project.

## Libraries

The libraries included in the project are:

#####  VT
Low level vector tile rendering library using OpenGL.
##### MapnikVT
A higher-level vector tile rendering library that uses Mapnik-like XML style definition language. Includes decoders for MapBox vector tiles and Torque tiles.
##### CartoCSS
An extended CartoCSS parser and translator to MapnikVT.
##### NML
Library for loading and rendering 3D models converted from Collada DAE files.
##### OSRM
A mobile-friendly routing library that uses routing graphs converted from OSRM data and provides optimized routing using Contraction Hierarchies.
##### SGRE
Simple GeoJSON Routing Engine - a routing engine designed for indoor scenarios, using GeoJSON input files for routing graphs. Works with all kinds of geometry (2D, 3D, lines, polygons).
##### Geocoding
A mobile-friendly geocoding library that uses special sqlite geocoding database converted from Pelias importer data. Optimized for fast searching with relatively small footprint.
##### MBVTBuilder
A basic library for building MapBox vector tiles from GeoJSON features.
