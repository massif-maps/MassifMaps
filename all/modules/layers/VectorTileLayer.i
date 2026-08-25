#ifndef _VECTORTILELAYER_I
#define _VECTORTILELAYER_I

%module VectorTileLayer

!proxy_imports(massif::VectorTileLayer, datasources.TileDataSource, datasources.components.TileData, layers.TileLayer, layers.VectorTileEventListener, vectortiles.VectorTileDecoder)

%{
#include "layers/VectorTileLayer.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_string.i>
%include <std_shared_ptr.i>
%include <massifswig.i>

%import "datasources/TileDataSource.i"
%import "layers/VectorTileEventListener.i"
%import "layers/TileLayer.i"
%import "vectortiles/VectorTileDecoder.i"

!enum(massif::VectorTileRenderOrder::VectorTileRenderOrder)
!polymorphic_shared_ptr(massif::VectorTileLayer, layers.VectorTileLayer)


!event(massif::VectorTileLayer, vectortile.clicked, payload(massif::VectorTileClickInfo), consumable)
!spec(massif::VectorTileLayer, layer, vector, alias(source, dataSource), alias(style, decoder))
%attribute(massif::VectorTileLayer, std::size_t, TileCacheCapacity, getTileCacheCapacity, setTileCacheCapacity)
%attribute(massif::VectorTileLayer, massif::VectorTileRenderOrder::VectorTileRenderOrder, LabelRenderOrder, getLabelRenderOrder, setLabelRenderOrder)
%attribute(massif::VectorTileLayer, massif::VectorTileRenderOrder::VectorTileRenderOrder, BuildingRenderOrder, getBuildingRenderOrder, setBuildingRenderOrder)
%attribute(massif::VectorTileLayer, float, ClickRadius, getClickRadius, setClickRadius)
%attribute(massif::VectorTileLayer, float, LayerBlendingSpeed, getLayerBlendingSpeed, setLayerBlendingSpeed)
%attribute(massif::VectorTileLayer, float, LabelBlendingSpeed, getLabelBlendingSpeed, setLabelBlendingSpeed)
%attributestring(massif::VectorTileLayer, std::string, RendererLayerFilter, getRendererLayerFilter, setRendererLayerFilter)
%attributestring(massif::VectorTileLayer, std::string, ClickHandlerLayerFilter, getClickHandlerLayerFilter, setClickHandlerLayerFilter)
!attributestring_polymorphic(massif::VectorTileLayer, vectortiles.VectorTileDecoder, TileDecoder, getTileDecoder)
// So a style parameter reads as one path from the layer: "style.params.water_color".
!alias(massif::VectorTileLayer, style, tileDecoder)
!attributestring_polymorphic(massif::VectorTileLayer, layers.VectorTileEventListener, VectorTileEventListener, getVectorTileEventListener, setVectorTileEventListener)
%std_exceptions(massif::VectorTileLayer::VectorTileLayer)
%ignore massif::VectorTileLayer::FetchTask;
%ignore massif::VectorTileLayer::getMinZoom;
%ignore massif::VectorTileLayer::getMaxZoom;
%ignore massif::VectorTileLayer::getCullDelay;

%include "layers/VectorTileLayer.h"

#endif
