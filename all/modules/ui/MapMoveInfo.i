#ifndef _MAPMOVEINFO_I
#define _MAPMOVEINFO_I

%module MapMoveInfo

!proxy_imports(massif::MapMoveInfo)

%{
// MapMoveReason.h is listed even though MapMoveInfo.h already includes it: the table generator
// reads the enum's constants out of the headers named HERE, and does not follow includes.
#include "ui/MapMoveReason.h"
#include "ui/MapMoveInfo.h"
#include <memory>
%}

%import <std_shared_ptr.i>
%include <massifswig.i>

!enum(massif::MapMoveReason::MapMoveReason)
!shared_ptr(massif::MapMoveInfo, ui.MapMoveInfo)

%attribute(massif::MapMoveInfo, massif::MapMoveReason::MapMoveReason, Reason, getReason)
%ignore massif::MapMoveInfo::MapMoveInfo;
!standard_equals(massif::MapMoveInfo);

%include "ui/MapMoveReason.h"
%include "ui/MapMoveInfo.h"

#endif
