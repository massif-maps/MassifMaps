#ifndef _HILLSHADERASTERTILELAYER_I
#define _HILLSHADERASTERTILELAYER_I

%module HillshadeRasterTileLayer

!proxy_imports(massif::HillshadeRasterTileLayer, core.MapPos, core.MapVec, core.MapPosVector, core.DoubleVector, datasources.TileDataSource, rastertiles.ElevationDecoder, graphics.Color, layers.CustomRasterTileLayer)

%{
#include "layers/HillshadeRasterTileLayer.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <massifswig.i>

%import "datasources/TileDataSource.i"
%import "rastertiles/ElevationDecoder.i"
%import "graphics/Color.i"
%import "layers/CustomRasterTileLayer.i"
%import "core/DoubleVector.i"

!enum(massif::HillshadeMethod::HillshadeMethod)
!polymorphic_shared_ptr(massif::HillshadeRasterTileLayer, layers.HillshadeRasterTileLayer)


!spec(massif::HillshadeRasterTileLayer, layer, hillshade, alias(source, dataSource))
%attribute(massif::HillshadeRasterTileLayer, float, Contrast, getContrast, setContrast)
%attribute(massif::HillshadeRasterTileLayer, float, HeightScale, getHeightScale, setHeightScale)
%attribute(massif::HillshadeRasterTileLayer, float, Exaggeration, getExaggeration, setExaggeration)
%attribute(massif::HillshadeRasterTileLayer, massif::MapVec, IlluminationDirection, getIlluminationDirection, setIlluminationDirection)
%attribute(massif::HillshadeRasterTileLayer, bool, IlluminationMapRotationEnabled, getIlluminationMapRotationEnabled, setIlluminationMapRotationEnabled)
%attribute(massif::HillshadeRasterTileLayer, bool, ExagerateHeightScaleEnabled, getExagerateHeightScaleEnabled, setExagerateHeightScaleEnabled)
%attribute(massif::HillshadeRasterTileLayer, bool, LegacyHeightScaleEnabled, isLegacyHeightScaleEnabled, setLegacyHeightScaleEnabled)
%attribute(massif::HillshadeRasterTileLayer, massif::HillshadeMethod::HillshadeMethod, HillshadeMethod, getHillshadeMethod, setHillshadeMethod)
%attributeval(massif::HillshadeRasterTileLayer, massif::Color, ShadowColor, getShadowColor, setShadowColor)
%attributeval(massif::HillshadeRasterTileLayer, massif::Color, HighlightColor, getHighlightColor, setHighlightColor)
%attributeval(massif::HillshadeRasterTileLayer, massif::Color, AccentColor, getAccentColor, setAccentColor)
%attributeval(massif::HillshadeRasterTileLayer, std::string, NormalMapLightingShader, getNormalMapLightingShader, setNormalMapLightingShader)
%attribute(massif::HillshadeRasterTileLayer, bool, ElevationEncodingEnabled, isElevationEncodingEnabled, setElevationEncodingEnabled)
%attribute(massif::HillshadeRasterTileLayer, bool, ContourEnabled, isContourEnabled, setContourEnabled)
%attribute(massif::HillshadeRasterTileLayer, float, ContourInterval, getContourInterval, setContourInterval)
%attributeval(massif::HillshadeRasterTileLayer, massif::Color, ContourColor, getContourColor, setContourColor)
%attribute(massif::HillshadeRasterTileLayer, float, ContourWidth, getContourWidth, setContourWidth)
%attribute(massif::HillshadeRasterTileLayer, bool, TerrainPaintEnabled, isTerrainPaintEnabled, setTerrainPaintEnabled)
%attribute(massif::HillshadeRasterTileLayer, bool, TerrainPaintFullDetailEnabled, isTerrainPaintFullDetailEnabled, setTerrainPaintFullDetailEnabled)
%std_exceptions(massif::HillshadeRasterTileLayer::HillshadeRasterTileLayer)

%include "layers/HillshadeRasterTileLayer.h"

#endif
