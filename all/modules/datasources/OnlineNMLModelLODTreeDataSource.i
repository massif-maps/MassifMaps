#ifndef _ONLINENMLMODELLODTREEDATASOURCE_I
#define _ONLINENMLMODELLODTREEDATASOURCE_I

%module OnlineNMLModelLODTreeDataSource

#ifdef _MASSIF_NMLMODELLODTREE_SUPPORT

!proxy_imports(massif::OnlineNMLModelLODTreeDataSource, datasources.NMLModelLODTreeDataSource, projections.Projection)

%{
#include "datasources/OnlineNMLModelLODTreeDataSource.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <massifswig.i>

%import "datasources/NMLModelLODTreeDataSource.i"

!polymorphic_shared_ptr(massif::OnlineNMLModelLODTreeDataSource, datasources.OnlineNMLModelLODTreeDataSource)

!spec(massif::OnlineNMLModelLODTreeDataSource, source, nml-lodtree-online, alias(url, serviceURL))

%include "datasources/OnlineNMLModelLODTreeDataSource.h"

#endif

#endif
