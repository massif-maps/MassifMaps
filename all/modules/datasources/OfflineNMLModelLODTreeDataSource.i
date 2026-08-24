#ifndef _OFFLINENMLMODELLODTREEDATASOURCE_I
#define _OFFLINENMLMODELLODTREEDATASOURCE_I

%module OfflineNMLModelLODTreeDataSource

#if defined(_MASSIF_NMLMODELLODTREE_SUPPORT) && defined(_MASSIF_OFFLINE_SUPPORT)

!proxy_imports(massif::OfflineNMLModelLODTreeDataSource, core.MapBounds, datasources.NMLModelLODTreeDataSource, projections.Projection)

%{
#include "datasources/OfflineNMLModelLODTreeDataSource.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <massifswig.i>

%import "datasources/NMLModelLODTreeDataSource.i"

!polymorphic_shared_ptr(massif::OfflineNMLModelLODTreeDataSource, datasources.OfflineNMLModelLODTreeDataSource)

!spec(massif::OfflineNMLModelLODTreeDataSource, source, nml-lodtree-offline, alias(path, path))

%std_io_exceptions(massif::OfflineNMLModelLODTreeDataSource::OfflineNMLModelLODTreeDataSource)

%include "datasources/OfflineNMLModelLODTreeDataSource.h"

#endif

#endif
