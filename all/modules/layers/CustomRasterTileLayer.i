#ifndef _CUSTOMRASTERTILELAYER_I
#define _CUSTOMRASTERTILELAYER_I

%module CustomRasterTileLayer

!proxy_imports(massif::CustomRasterTileLayer, datasources.TileDataSource, layers.RasterTileLayer, core.Variant, core.StringVariantMap)

%{
#include "layers/CustomRasterTileLayer.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <std_string.i>
%include <massifswig.i>

%import "core/Variant.i"
%import "datasources/TileDataSource.i"
%import "layers/RasterTileLayer.i"

!polymorphic_shared_ptr(massif::CustomRasterTileLayer, layers.CustomRasterTileLayer)

!spec(massif::CustomRasterTileLayer, layer, custom-raster, alias(source, dataSource))
// Wired after construction, like the composite layer's slots; its source is usually shared.
!method(massif::CustomRasterTileLayer, addDataSource, arg(dataSource, handle), returns(void))
%attributestring(massif::CustomRasterTileLayer, std::string, ShaderSource, getShaderSource, setShaderSource)
%attributeval(massif::CustomRasterTileLayer, %arg(std::map<std::string, massif::Variant>), ShaderParameters, getShaderParameters, setShaderParameters)
%std_exceptions(massif::CustomRasterTileLayer::CustomRasterTileLayer)
%std_exceptions(massif::CustomRasterTileLayer::addDataSource)

%include "layers/CustomRasterTileLayer.h"

#endif
