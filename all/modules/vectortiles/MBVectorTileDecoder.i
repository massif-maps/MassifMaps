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


!method(massif::MBVectorTileDecoder, setStyleParameter, arg(name, string), arg(value, string), returns(bool))
!method(massif::MBVectorTileDecoder, getStyleParameter, arg(name, string), returns(string))
// Several at once. The `styleParameters` property is the list of NAMES the style declares.
!method(massif::MBVectorTileDecoder, setStyleParameters, arg(params, json), returns(void))
// And as a property bag, which is what an app writes: set(style, "params.water_color", "#0af").
// setStyleParameter answers whether the style declares the parameter, so an undeclared one is
// refused rather than dropped.
!indexed(massif::MBVectorTileDecoder, params, getStyleParameter, setStyleParameter, returns(bool))
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
