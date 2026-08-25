#ifndef _TILEDATA_I
#define _TILEDATA_I

%module TileData

!proxy_imports(massif::TileData, core.BinaryData, core.Variant)

%{
#include "datasources/components/TileData.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <massifswig.i>

%import "core/BinaryData.i"
%import "core/Variant.i"

!shared_ptr(massif::TileData, datasources.components.TileData)

%attribute(massif::TileData, long long, MaxAge, getMaxAge, setMaxAge)
%attribute(massif::TileData, bool, ReplaceWithParent, isReplaceWithParent, setReplaceWithParent)
%attributestring(massif::TileData, std::shared_ptr<massif::BinaryData>, Data, getData)
// The raw-pixel constructor is reachable from a binding too, so a Java or Objective-C source that
// already has pixels skips the PNG encode the SDK would immediately undo.
%attribute(massif::TileData, bool, RawPixels, isRawPixels)
%attribute(massif::TileData, int, Width, getWidth)
%attribute(massif::TileData, int, Height, getHeight)

// The meta data of the source that produced the tile - "dem_encoding" tells the consumer how to
// decode a DEM tile, and behind a wrapper source only the tile knows which source answered.
!method(massif::TileData, getMetaDataElement, arg(key, string), returns(json))

// The map itself is shared and immutable, so it is handed over as a raw pointer that no binding
// can express; the per-key accessors above are the public way in.
%ignore massif::TileData::getMetaData;
%ignore massif::TileData::setMetaData;

!standard_equals(massif::TileData);

%include "datasources/components/TileData.h"

#endif
