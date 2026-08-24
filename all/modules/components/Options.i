#ifndef _OPTIONS_I
#define _OPTIONS_I

#pragma SWIG nowarn=325
#pragma SWIG nowarn=401

%module Options

!proxy_imports(massif::Options, core.MapBounds, core.MapRange, core.MapVec, core.ScreenPos, components.TerrainOptions, components.SkyOptions, components.FogOptions, components.LightOptions, graphics.Bitmap, graphics.Color, projections.Projection)

%{
#include "components/Options.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <massifswig.i>

!enum(massif::RenderProjectionMode::RenderProjectionMode)
!enum(massif::PanningMode::PanningMode)
!enum(massif::PivotMode::PivotMode)
!enum(massif::FreeRoamMode::FreeRoamMode)
!enum(massif::PanningSpeedMode::PanningSpeedMode)
!shared_ptr(massif::Options, components.Options)

// The map events. They fire on whatever handle the Options were registered under.
!event(massif::Options, map.idle)
!event(massif::Options, map.moved, payload(massif::MapMoveInfo))
!event(massif::Options, map.stable, payload(massif::MapMoveInfo))
!event(massif::Options, map.interaction, payload(massif::MapInteractionInfo))
!event(massif::Options, map.clicked, payload(massif::MapClickInfo))
%import "core/MapBounds.i"
%import "core/MapRange.i"
%import "core/MapVec.i"
%import "core/ScreenPos.i"
%import "components/TerrainOptions.i"
%import "components/SkyOptions.i"
%import "components/FogOptions.i"
%import "components/LightOptions.i"
%import "graphics/Bitmap.i"
%import "graphics/Color.i"
%import "projections/Projection.i"

// Declared as attributes so the facade's property table can traverse into them: "fogOptions.rangeStart"
// resolves without anything being hand-listed. The getters and setters are unaffected.
%attributestring(massif::Options, std::shared_ptr<massif::TerrainOptions>, TerrainOptions, getTerrainOptions, setTerrainOptions)
%attributestring(massif::Options, std::shared_ptr<massif::SkyOptions>, SkyOptions, getSkyOptions, setSkyOptions)
%attributestring(massif::Options, std::shared_ptr<massif::FogOptions>, FogOptions, getFogOptions, setFogOptions)
%attributestring(massif::Options, std::shared_ptr<massif::LightOptions>, LightOptions, getLightOptions, setLightOptions)
%attribute(massif::Options, int, FieldOfViewY, getFieldOfViewY, setFieldOfViewY)
%attribute(massif::Options, bool, KineticZoom, isKineticZoom, setKineticZoom)
%attribute(massif::Options, bool, Rotatable, isRotatable, setRotatable)
%attribute(massif::Options, bool, UserInput, isUserInput, setUserInput)
%attribute(massif::Options, massif::PanningSpeedMode::PanningSpeedMode, PanningSpeedMode, getPanningSpeedMode, setPanningSpeedMode)
%attribute(massif::Options, massif::FreeRoamMode::FreeRoamMode, FreeRoamMode, getFreeRoamMode, setFreeRoamMode)
%attribute(massif::Options, float, FreeRoamLookSensitivity, getFreeRoamLookSensitivity, setFreeRoamLookSensitivity)
%attribute(massif::Options, float, FreeRoamMoveSpeed, getFreeRoamMoveSpeed, setFreeRoamMoveSpeed)
%attribute(massif::Options, bool, DebugTileBorders, isDebugTileBorders, setDebugTileBorders)
%attribute(massif::Options, bool, ClickTypeDetection, isClickTypeDetection, setClickTypeDetection)
%attribute(massif::Options, bool, DoubleClickDetection, isDoubleClickDetection, setDoubleClickDetection)
%attribute(massif::Options, float, LongClickDuration, getLongClickDuration, setLongClickDuration)
%attribute(massif::Options, float, DoubleClickMaxDuration, getDoubleClickMaxDuration, setDoubleClickMaxDuration)
%attribute(massif::Options, bool, KineticPan, isKineticPan, setKineticPan)
%attribute(massif::Options, bool, KineticRotation, isKineticRotation, setKineticRotation)
%attribute(massif::Options, bool, SeamlessPanning, isSeamlessPanning, setSeamlessPanning)
%attribute(massif::Options, bool, RestrictedPanning, isRestrictedPanning, setRestrictedPanning)
%attribute(massif::Options, bool, TiltGestureReversed, isTiltGestureReversed, setTiltGestureReversed)
%attribute(massif::Options, bool, ZoomGestures, isZoomGestures, setZoomGestures)
%attribute(massif::Options, bool, RotationGestures, isRotationGestures, setRotationGestures)
%attribute(massif::Options, bool, LayersLabelsProcessedInReverseOrder, isLayersLabelsProcessedInReverseOrder, setLayersLabelsProcessedInReverseOrder)
%attributeval(massif::Options, massif::MapRange, ZoomRange, getZoomRange, setZoomRange)
%attributeval(massif::Options, massif::MapRange, TiltRange, getTiltRange, setTiltRange)
%attributeval(massif::Options, massif::MapBounds, PanBounds, getPanBounds, setPanBounds)
%attributeval(massif::Options, massif::ScreenPos, FocusPointOffset, getFocusPointOffset, setFocusPointOffset)
%attributeval(massif::Options, massif::Color, AmbientLightColor, getAmbientLightColor, setAmbientLightColor)
%attributeval(massif::Options, massif::Color, MainLightColor, getMainLightColor, setMainLightColor)
%attributeval(massif::Options, massif::MapVec, MainLightDirection, getMainLightDirection, setMainLightDirection)
!attributestring_polymorphic(massif::Options, projections.Projection, BaseProjection, getBaseProjection, setBaseProjection)
%attribute(massif::Options, massif::RenderProjectionMode::RenderProjectionMode, RenderProjectionMode, getRenderProjectionMode, setRenderProjectionMode)
%attribute(massif::Options, massif::PanningMode::PanningMode, PanningMode, getPanningMode, setPanningMode)
%attribute(massif::Options, massif::PivotMode::PivotMode, PivotMode, getPivotMode, setPivotMode)
%attributeval(massif::Options, massif::Color, ClearColor, getClearColor, setClearColor)
%attributeval(massif::Options, massif::Color, SkyColor, getSkyColor, setSkyColor)
%attributestring(massif::Options, std::shared_ptr<massif::Bitmap>, BackgroundBitmap, getBackgroundBitmap, setBackgroundBitmap)
%attribute(massif::Options, int, EnvelopeThreadPoolSize, getEnvelopeThreadPoolSize, setEnvelopeThreadPoolSize)
%attribute(massif::Options, int, TileThreadPoolSize, getTileThreadPoolSize, setTileThreadPoolSize)
%attribute(massif::Options, int, TileDrawSize, getTileDrawSize, setTileDrawSize)
%attribute(massif::Options, float, TileLODFactor, getTileLODFactor, setTileLODFactor)
%attribute(massif::Options, float, TileLODForeshorteningLimit, getTileLODForeshorteningLimit, setTileLODForeshorteningLimit)
%attribute(massif::Options, float, DPI, getDPI, setDPI)
%attribute(massif::Options, float, DrawDistance, getDrawDistance, setDrawDistance)
%std_exceptions(massif::Options::setBaseProjection)
%std_exceptions(massif::Options::setTiltRange)
%std_exceptions(massif::Options::setZoomRange)
%std_exceptions(massif::Options::setPanBounds)
%ignore massif::Options::Options;
%ignore massif::Options::getProjectionSurface;
%ignore massif::Options::getSkyBitmap;
%ignore massif::Options::getAdjustedInternalPanBounds;
%ignore massif::Options::OnChangeListener;
%ignore massif::Options::registerOnChangeListener;
%ignore massif::Options::unregisterOnChangeListener;
%ignore massif::Options::GetDefaultBackgroundBitmap;
!standard_equals(massif::Options);

%include "components/Options.h"

#endif
