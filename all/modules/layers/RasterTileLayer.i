#ifndef _RASTERTILELAYER_I
#define _RASTERTILELAYER_I

%module RasterTileLayer

!proxy_imports(massif::RasterTileLayer, datasources.TileDataSource, layers.TileLayer, layers.RasterTileEventListener)

%{
#include "layers/RasterTileLayer.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <massifswig.i>

%import "datasources/TileDataSource.i"
%import "layers/RasterTileEventListener.i"
%import "layers/TileLayer.i"

!enum(massif::RasterTileFilterMode::RasterTileFilterMode)
!polymorphic_shared_ptr(massif::RasterTileLayer, layers.RasterTileLayer)


!spec(massif::RasterTileLayer, layer, raster, alias(source, dataSource))
%attribute(massif::RasterTileLayer, std::size_t, TextureCacheCapacity, getTextureCacheCapacity, setTextureCacheCapacity)
%attribute(massif::RasterTileLayer, massif::RasterTileFilterMode::RasterTileFilterMode, TileFilterMode, getTileFilterMode, setTileFilterMode)
%attribute(massif::RasterTileLayer, float, TileBlendingSpeed, getTileBlendingSpeed, setTileBlendingSpeed)
!attributestring_polymorphic(massif::RasterTileLayer, layers.RasterTileEventListener, RasterTileEventListener, getRasterTileEventListener, setRasterTileEventListener)
%std_exceptions(massif::RasterTileLayer::RasterTileLayer)
%ignore massif::RasterTileLayer::FetchTask;
%ignore massif::RasterTileLayer::getMinZoom;
%ignore massif::RasterTileLayer::getMaxZoom;

%include "layers/RasterTileLayer.h"

#endif
