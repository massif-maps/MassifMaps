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
%attribute(massif::CompositeVectorTileLayer, bool, SinglePassRenderingEnabled, isSinglePassRenderingEnabled, setSinglePassRenderingEnabled)
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
