#ifndef _VECTORTILEDECODER_I
#define _VECTORTILEDECODER_I

#pragma SWIG nowarn=325

%module VectorTileDecoder

!proxy_imports(massif::VectorTileDecoder, core.BinaryData, graphics.Color)

%{
#include "vectortiles/VectorTileDecoder.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <massifswig.i>

%import "core/BinaryData.i"
%import "graphics/Color.i"

!polymorphic_shared_ptr(massif::VectorTileDecoder, vectortiles.VectorTileDecoder)

%csmethodmodifiers massif::VectorTileDecoder::MinZoom "public virtual"
%attribute(massif::VectorTileDecoder, int, MinZoom, getMinZoom)
%csmethodmodifiers massif::VectorTileDecoder::MaxZoom "public virtual"
%attribute(massif::VectorTileDecoder, int, MaxZoom, getMaxZoom)
%ignore massif::VectorTileDecoder::decodeFeature;
%ignore massif::VectorTileDecoder::decodeFeatures;
%ignore massif::VectorTileDecoder::decodeTile;
%ignore massif::VectorTileDecoder::getMapSettings;
%ignore massif::VectorTileDecoder::getSymbolizerContextSettings;
%ignore massif::VectorTileDecoder::setPixelScale;
%ignore massif::VectorTileDecoder::setTileSize;
%ignore massif::VectorTileDecoder::OnChangeListener;
%ignore massif::VectorTileDecoder::registerOnChangeListener;
%ignore massif::VectorTileDecoder::unregisterOnChangeListener;
!standard_equals(massif::VectorTileDecoder);

%include "vectortiles/VectorTileDecoder.h"

#endif
