#ifndef _NMLMODELLODTREECLICKINFO_I
#define _NMLMODELLODTREECLICKINFO_I

%module NMLModelLODTreeClickInfo

#ifdef _MASSIF_NMLMODELLODTREE_SUPPORT

!proxy_imports(massif::NMLModelLODTreeClickInfo, core.MapPos, core.StringMap, layers.Layer, ui.ClickInfo)

%{
#include "ui/NMLModelLODTreeClickInfo.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <std_string.i>
%include <massifswig.i>

%import "ui/ClickInfo.i"
%import "core/MapPos.i"
%import "core/StringMap.i"
%import "layers/Layer.i"

!shared_ptr(massif::NMLModelLODTreeClickInfo, ui.NMLModelLODTreeClickInfo)

%attribute(massif::NMLModelLODTreeClickInfo, massif::ClickType::ClickType, ClickType, getClickType)
%attributeval(massif::NMLModelLODTreeClickInfo, massif::ClickInfo, ClickInfo, getClickInfo)
%attributeval(massif::NMLModelLODTreeClickInfo, massif::MapPos, ClickPos, getClickPos)
%attributeval(massif::NMLModelLODTreeClickInfo, massif::MapPos, ElementClickPos, getElementClickPos)
%attributeval(massif::NMLModelLODTreeClickInfo, %arg(std::map<std::string, std::string>), MetaData, getMetaData)
!attributestring_polymorphic(massif::NMLModelLODTreeClickInfo, layers.Layer, Layer, getLayer)
%ignore massif::NMLModelLODTreeClickInfo::NMLModelLODTreeClickInfo;
!standard_equals(massif::NMLModelLODTreeClickInfo);

%include "ui/NMLModelLODTreeClickInfo.h"

#endif

#endif
