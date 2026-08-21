#ifndef _VECTORTILECLICKINFO_I
#define _VECTORTILECLICKINFO_I

%module VectorTileClickInfo

!proxy_imports(massif::VectorTileClickInfo, core.MapPos, core.MapTile, geometry.VectorTileFeature, layers.Layer, ui.ClickInfo)

%{
#include "ui/VectorTileClickInfo.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <std_string.i>
%include <massifswig.i>

%import "ui/ClickInfo.i"
%import "core/MapPos.i"
%import "core/MapTile.i"
%import "geometry/VectorTileFeature.i"
%import "layers/Layer.i"

!shared_ptr(massif::VectorTileClickInfo, ui.VectorTileClickInfo)

%attribute(massif::VectorTileClickInfo, massif::ClickType::ClickType, ClickType, getClickType)
%attributeval(massif::VectorTileClickInfo, massif::ClickInfo, ClickInfo, getClickInfo)
%attributeval(massif::VectorTileClickInfo, massif::MapPos, ClickPos, getClickPos)
%attributeval(massif::VectorTileClickInfo, massif::MapPos, FeatureClickPos, getFeatureClickPos)
%attributeval(massif::VectorTileClickInfo, massif::MapPos, FeaturePos, getFeaturePos)
%attributeval(massif::VectorTileClickInfo, massif::MapTile, MapTile, getMapTile)
%attribute(massif::VectorTileClickInfo, long long, FeatureId, getFeatureId)
%attribute(massif::VectorTileClickInfo, int, FeaturePosIndex, getFeaturePosIndex)
%attributestring(massif::VectorTileClickInfo, std::shared_ptr<massif::VectorTileFeature>, Feature, getFeature)
%attributestring(massif::VectorTileClickInfo, std::string, FeatureLayerName, getFeatureLayerName)
!attributestring_polymorphic(massif::VectorTileClickInfo, layers.Layer, Layer, getLayer)
%ignore massif::VectorTileClickInfo::VectorTileClickInfo;
!standard_equals(massif::VectorTileClickInfo);

%include "ui/VectorTileClickInfo.h"

#endif
