#ifndef _MBVECTORTILEDECODER_I
#define _MBVECTORTILEDECODER_I

%module MBVectorTileDecoder

!proxy_imports(massif::MBVectorTileDecoder, core.BinaryData, core.StringVector, core.StringMap, graphics.Color, styles.CompiledStyleSet, styles.CartoCSSStyleSet, vectortiles.VectorTileDecoder)

%{
#include "vectortiles/MBVectorTileDecoder.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <std_vector.i>
%include <std_map.i>
%include <massifswig.i>

%import "core/StringMap.i"
%import "core/BinaryData.i"
%import "styles/CompiledStyleSet.i"
%import "styles/CartoCSSStyleSet.i"
%import "vectortiles/VectorTileDecoder.i"

!enum(massif::TileFormat::TileFormat)
!polymorphic_shared_ptr(massif::MBVectorTileDecoder, vectortiles.MBVectorTileDecoder)


!spec(massif::MBVectorTileDecoder, style, mbvt, alias(cartocss, cartoCSSStyleSet), alias(project, compiledStyleSet))
%attributeval(massif::MBVectorTileDecoder, std::vector<std::string>, StyleParameters, getStyleParameters)
%attributeval(massif::MBVectorTileDecoder, std::vector<std::string>, StyleLayerNames, getStyleLayerNames)
%attributestring(massif::MBVectorTileDecoder, std::shared_ptr<massif::CompiledStyleSet>, CompiledStyle, getCompiledStyleSet, setCompiledStyleSet)
%attributestring(massif::MBVectorTileDecoder, std::shared_ptr<massif::CartoCSSStyleSet>, CartoCSSStyle, getCartoCSSStyleSet, setCartoCSSStyleSet)
%attribute(massif::MBVectorTileDecoder, bool, FeatureIdOverride, isFeatureIdOverride, setFeatureIdOverride)
%attribute(massif::MBVectorTileDecoder, massif::TileFormat::TileFormat, TileFormat, getTileFormat, setTileFormat)
%std_exceptions(massif::MBVectorTileDecoder::MBVectorTileDecoder)
%std_exceptions(massif::MBVectorTileDecoder::setCompiledStyleSet)
%std_exceptions(massif::MBVectorTileDecoder::setCartoCSSStyleSet)
%std_exceptions(massif::MBVectorTileDecoder::getStyleParameter)
%std_exceptions(massif::MBVectorTileDecoder::setStyleParameter)
%std_exceptions(massif::MBVectorTileDecoder::setStyleParameters)
%std_exceptions(massif::MBVectorTileDecoder::setJSONStyleParameters)
%ignore massif::MBVectorTileDecoder::isCartoCSSLayerNamesIgnored;
%ignore massif::MBVectorTileDecoder::setCartoCSSLayerNamesIgnored;
%ignore massif::MBVectorTileDecoder::getLayerNameOverride;
%ignore massif::MBVectorTileDecoder::setLayerNameOverride;
%ignore massif::MBVectorTileDecoder::decodeFeature;
%ignore massif::MBVectorTileDecoder::decodeFeatures;
%ignore massif::MBVectorTileDecoder::decodeTile;
%ignore massif::MBVectorTileDecoder::getMapSettings;
%ignore massif::MBVectorTileDecoder::getSymbolizerContextSettings;
%ignore massif::MBVectorTileDecoder::setPixelScale;
%ignore massif::MBVectorTileDecoder::loadMapnikMap;
%ignore massif::MBVectorTileDecoder::loadCartoCSSMap;
%ignore massif::MBVectorTileDecoder::resolveLayerConfig;
%ignore massif::MBVectorTileDecoder::getStyleLayerZoomRange;

%include "vectortiles/MBVectorTileDecoder.h"

#endif
