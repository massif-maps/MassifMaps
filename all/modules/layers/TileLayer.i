#ifndef _TILELAYER_I
#define _TILELAYER_I

#pragma SWIG nowarn=402

%module TileLayer

!proxy_imports(massif::TileLayer, projections.Projection, core.MapPos, core.MapTile, core.MapBounds, datasources.TileDataSource, layers.TileLoadListener, layers.UTFGridEventListener, layers.Layer)

%{
#include "layers/TileLayer.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <massifswig.i>

%import "datasources/TileDataSource.i"
%import "projections/Projection.i"
%import "layers/Layer.i"
%import "layers/TileLoadListener.i"
%import "layers/UTFGridEventListener.i"

!enum(massif::TileSubstitutionPolicy::TileSubstitutionPolicy)
!polymorphic_shared_ptr(massif::TileLayer, layers.TileLayer)

// true also drops the persistent cache, not just the in-memory one.
!method(massif::TileLayer, clearTileCaches, arg(all, bool), returns(void))
// How the facade learns what coordinate system this layer's positions - a click, a feature - are
// in. dataSource is already declared below.
!attributestring_polymorphic(massif::TileLayer, projections.Projection, Projection, getProjection)
%attribute(massif::TileLayer, int, FrameNr, getFrameNr, setFrameNr)
%attribute(massif::TileLayer, bool, Preloading, isPreloading, setPreloading)
%attribute(massif::TileLayer, bool, SynchronizedRefresh, isSynchronizedRefresh, setSynchronizedRefresh)
%attribute(massif::TileLayer, massif::TileSubstitutionPolicy::TileSubstitutionPolicy, TileSubstitutionPolicy, getTileSubstitutionPolicy, setTileSubstitutionPolicy)
%attribute(massif::TileLayer, float, ZoomLevelBias, getZoomLevelBias, setZoomLevelBias)
%attribute(massif::TileLayer, int, MaxOverzoomLevel, getMaxOverzoomLevel, setMaxOverzoomLevel)
%attribute(massif::TileLayer, int, MaxUnderzoomLevel, getMaxUnderzoomLevel, setMaxUnderzoomLevel)
!attributestring_polymorphic(massif::TileLayer, datasources.TileDataSource, DataSource, getDataSource)
// The spec key is "source"; the property path is the same word.
!alias(massif::TileLayer, source, dataSource)
!attributestring_polymorphic(massif::TileLayer, datasources.TileDataSource, UTFGridDataSource, getUTFGridDataSource, setUTFGridDataSource)
!attributestring_polymorphic(massif::TileLayer, layers.TileLoadListener, TileLoadListener, getTileLoadListener, setTileLoadListener)
!attributestring_polymorphic(massif::TileLayer, layers.UTFGridEventListener, UTFGridEventListener, getUTFGridEventListener, setUTFGridEventListener)
%std_exceptions(massif::TileLayer::TileLayer)
%ignore massif::TileLayer::setTerrainDepthWriteMode;
%ignore massif::TileLayer::drapeStackSignature;
%ignore massif::TileLayer::paintsEveryDrapeTile;
%ignore massif::TileLayer::setTerrainPaintTiles;
%ignore massif::TileLayer::setTerrainGroundTiles;
%ignore massif::TileLayer::setTerrainLayerOrdinalBase;
%ignore massif::TileLayer::setTerrainStackOrdinalSpan;
%ignore massif::TileLayer::getStyleLayerCount;
%ignore massif::TileLayer::renderTerrainGround;
%ignore massif::TileLayer::blitDrapeTexture;
%ignore massif::TileLayer::shadowCasterFadeSignature;
%ignore massif::TileLayer::prepareTerrainDrapeFrame;
// Internal cross-layer terrain drape / shadow plumbing, driven by MapRenderer.
%ignore massif::TileLayer::collectDrapeLayers;
%ignore massif::TileLayer::setExternalDrapeTarget;
%ignore massif::TileLayer::setExternalDrapeTiles;
%ignore massif::TileLayer::collectDrapeTiles;
%ignore massif::TileLayer::bakeDrapeTile;
%ignore massif::TileLayer::renderDrapedSurface;
%ignore massif::TileLayer::renderDrapedSurfaceFill;
%ignore massif::TileLayer::calculateShadowViewProj;
%ignore massif::TileLayer::renderShadowCasters;
%ignore massif::TileLayer::setTerrainShadowMap;
%ignore massif::TileLayer::setTerrainSunLighting;
%ignore massif::TileLayer::setTerrainRenderOrder;
%ignore massif::TileLayer::getBackgroundColor;
%ignore massif::TileLayer::FetchTaskBase;
%ignore massif::TileLayer::FetchingTiles;
%ignore massif::TileLayer::DataSourceListener;
%ignore massif::TileLayer::UTFGridTile;
%ignore massif::TileLayer::getMinZoom;
%ignore massif::TileLayer::getMaxZoom;

%include "layers/TileLayer.h"

#endif
