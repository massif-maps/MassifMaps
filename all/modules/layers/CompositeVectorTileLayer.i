#ifndef _COMPOSITEVECTORTILELAYER_I
#define _COMPOSITEVECTORTILELAYER_I

%module CompositeVectorTileLayer

!proxy_imports(massif::CompositeVectorTileLayer, datasources.TileDataSource, layers.VectorTileLayer, vectortiles.VectorTileDecoder, rastertiles.ElevationDecoder, core.StringVector)

%{
#include "layers/CompositeVectorTileLayer.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_string.i>
%include <std_vector.i>
%include <std_shared_ptr.i>
%include <massifswig.i>

%import "layers/VectorTileLayer.i"
%import "datasources/TileDataSource.i"
%import "vectortiles/VectorTileDecoder.i"
%import "rastertiles/ElevationDecoder.i"
%import "core/StringVector.i"

!enum(massif::CompositeSourceType::CompositeSourceType)
!polymorphic_shared_ptr(massif::CompositeVectorTileLayer, layers.CompositeVectorTileLayer)


!spec(massif::CompositeVectorTileLayer, layer, composite-vector, alias(source, dataSource), alias(style, decoder))

// Wired after construction: a slot is a style name and its source is usually shared (e.g. the DEM).
// `type` crosses as an int: the facade has no enum argument kind.
!method(massif::CompositeVectorTileLayer, addExternalDataSource, arg(name, string), arg(dataSource, handle), arg(type, int), returns(void))
!method(massif::CompositeVectorTileLayer, addVectorDataSource, arg(name, string), arg(dataSource, handle), returns(void))
!method(massif::CompositeVectorTileLayer, removeExternalDataSource, arg(name, string), returns(bool))
// The other half of the "why does a slot draw nothing" check: compared against the decoder's
// styleLayerNames, a registered name missing from the style is a source with nowhere to be drawn.
!method(massif::CompositeVectorTileLayer, getExternalDataSourceNames, returns(json))
!method(massif::CompositeVectorTileLayer, setExternalDataSourceZoomLevelBias, arg(name, string), arg(bias, float), returns(void))
!method(massif::CompositeVectorTileLayer, setExternalDataSourceMaxOverzoomLevel, arg(name, string), arg(level, int), returns(void))
// Reaches child settings the config symbolizer does not carry (e.g. a hillshade shader);
// applyConfig never writes them, so they survive the per-frame pass.
!method(massif::CompositeVectorTileLayer, getExternalChildLayer, arg(name, string), returns(object, massif::Layer))
// Internal, read by MapRenderer for the 2D/3D switch.
%ignore massif::CompositeVectorTileLayer::getTerrainDecodePendingCount;
// Virtual overrides: VectorTileLayer's binding already dispatches here; wrapping them again duplicates the property.
%ignore massif::CompositeVectorTileLayer::setVectorTileEventListener;
%ignore massif::CompositeVectorTileLayer::setClickRadius;
%ignore massif::CompositeVectorTileLayer::setClickHandlerLayerFilter;
%std_exceptions(massif::CompositeVectorTileLayer::CompositeVectorTileLayer)
%std_exceptions(massif::CompositeVectorTileLayer::addExternalDataSource)
%std_exceptions(massif::CompositeVectorTileLayer::addVectorDataSource)
%std_exceptions(massif::CompositeVectorTileLayer::setExternalDataSourceZoomLevelBias)
%std_exceptions(massif::CompositeVectorTileLayer::getExternalDataSourceZoomLevelBias)
%std_exceptions(massif::CompositeVectorTileLayer::clearExternalDataSourceZoomLevelBias)
%std_exceptions(massif::CompositeVectorTileLayer::setExternalDataSourceMaxOverzoomLevel)
%std_exceptions(massif::CompositeVectorTileLayer::getExternalDataSourceMaxOverzoomLevel)

%include "layers/CompositeVectorTileLayer.h"

#endif
