#ifndef _NMLMODELLODTREEDATASOURCE_I
#define _NMLMODELLODTREEDATASOURCE_I

#pragma SWIG nowarn=325
#pragma SWIG nowarn=401

%module NMLModelLODTreeDataSource

#ifdef _MASSIF_NMLMODELLODTREE_SUPPORT

!proxy_imports(massif::NMLModelLODTreeDataSource, core.MapBounds, projections.Projection)

%{
#include "datasources/NMLModelLODTreeDataSource.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <massifswig.i>

%import "core/MapBounds.i"
%import "projections/Projection.i"

!polymorphic_shared_ptr(massif::NMLModelLODTreeDataSource, datasources.NMLModelLODTreeDataSource)

%attributeval(massif::NMLModelLODTreeDataSource, massif::MapBounds, DataExtent, getDataExtent)
!attributestring_polymorphic(massif::NMLModelLODTreeDataSource, projections.Projection, Projection, getProjection);
%ignore massif::NMLModelLODTreeDataSource::MapTile;
%ignore massif::NMLModelLODTreeDataSource::loadMapTiles;
%ignore massif::NMLModelLODTreeDataSource::loadModelLODTree;
%ignore massif::NMLModelLODTreeDataSource::loadMesh;
%ignore massif::NMLModelLODTreeDataSource::loadTexture;

%include "datasources/NMLModelLODTreeDataSource.h"

#endif

#endif
