#ifndef _TERRAINOPTIONS_I
#define _TERRAINOPTIONS_I

%module TerrainOptions

!proxy_imports(massif::TerrainOptions, core.MapPos, core.MapPosVector, core.DoubleVector, datasources.TileDataSource, graphics.Color, rastertiles.ElevationDecoder)

%{
#include "components/TerrainOptions.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_string.i>
%include <std_shared_ptr.i>
%include <massifswig.i>

%import "core/MapPos.i"
%import "core/DoubleVector.i"
%import "graphics/Color.i"
%import "datasources/TileDataSource.i"
%import "rastertiles/ElevationDecoder.i"

!shared_ptr(massif::TerrainOptions, components.TerrainOptions)

// 3D terrain from an elevation source. Only the source is a constructor argument: the elevation
// decoder is picked from the source's own `encoding`, so a spec never names one.
!spec(massif::TerrainOptions, options, terrain, alias(source, dataSource))

%attribute(massif::TerrainOptions, bool, Enabled, isEnabled, setEnabled)
%attribute(massif::TerrainOptions, float, Exaggeration, getExaggeration, setExaggeration)
%attribute(massif::TerrainOptions, bool, SeamlessTileEdgesEnabled, isSeamlessTileEdgesEnabled, setSeamlessTileEdgesEnabled)
%attribute(massif::TerrainOptions, bool, ElevationPrefetchEnabled, isElevationPrefetchEnabled, setElevationPrefetchEnabled)
%attribute(massif::TerrainOptions, int, MeshResolution, getMeshResolution, setMeshResolution)
%attribute(massif::TerrainOptions, bool, TileEdgeStitchingEnabled, isTileEdgeStitchingEnabled, setTileEdgeStitchingEnabled)
%attribute(massif::TerrainOptions, bool, DrapeFillsEnabled, isDrapeFillsEnabled, setDrapeFillsEnabled)
%attribute(massif::TerrainOptions, bool, DrapeLinesEnabled, isDrapeLinesEnabled, setDrapeLinesEnabled)
%attribute(massif::TerrainOptions, int, DrapeResolution, getDrapeResolution, setDrapeResolution)
%attributestring(massif::TerrainOptions, std::string, NoDrapeLayerFilter, getNoDrapeLayerFilter, setNoDrapeLayerFilter)
%attribute(massif::TerrainOptions, int, MinZoom, getMinZoom, setMinZoom)
%attribute(massif::TerrainOptions, int, MaxTileZoomOffset, getMaxTileZoomOffset, setMaxTileZoomOffset)
%attributeval(massif::TerrainOptions, massif::Color, BackgroundColor, getBackgroundColor, setBackgroundColor)
%attribute(massif::TerrainOptions, float, ViewDistanceFactor, getViewDistanceFactor, setViewDistanceFactor)
%attribute(massif::TerrainOptions, float, ViewDistance, getViewDistance, setViewDistance)
%attribute(massif::TerrainOptions, int, MaxTileZoomCoarsening, getMaxTileZoomCoarsening, setMaxTileZoomCoarsening)
%attribute(massif::TerrainOptions, float, DepthBias, getDepthBias, setDepthBias)
// How far above the ground the camera is kept, in metres, and how long the clamp takes. Both
// existed in C++ only, so no binding could get a camera close to a slope - which is exactly what
// composing a 3D view needs.
%attribute(massif::TerrainOptions, float, CameraClearance, getCameraClearance, setCameraClearance)
%attribute(massif::TerrainOptions, float, CameraClampDuration, getCameraClampDuration, setCameraClampDuration)
%attribute(massif::TerrainOptions, bool, BillboardOcclusionEnabled, isBillboardOcclusionEnabled, setBillboardOcclusionEnabled)
%attribute(massif::TerrainOptions, float, BillboardOcclusionTolerance, getBillboardOcclusionTolerance, setBillboardOcclusionTolerance)
%attribute(massif::TerrainOptions, float, TextOcclusionOpacity, getTextOcclusionOpacity, setTextOcclusionOpacity)
%attributestring(massif::TerrainOptions, std::string, SurfaceShaderSource, getSurfaceShaderSource, setSurfaceShaderSource)
%std_exceptions(massif::TerrainOptions::TerrainOptions)

%ignore massif::TerrainOptions::getSurfaceParameters;
%ignore massif::TerrainOptions::getSurfaceColorParameters;

%ignore massif::TerrainOptions::OnChangeListener;
%ignore massif::TerrainOptions::registerOnChangeListener;
%ignore massif::TerrainOptions::unregisterOnChangeListener;
%ignore massif::TerrainOptions::getElevationManager;
%ignore massif::TerrainOptions::getElevationCacheCapacity;
%ignore massif::TerrainOptions::setElevationCacheCapacity;

%include "components/TerrainOptions.h"

#endif
