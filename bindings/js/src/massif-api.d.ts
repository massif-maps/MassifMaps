// GENERATED FILE - do not edit by hand.
// Regenerate with `npm run typings.api` (scripts/api-typings), which reads the SDK's
// docs/api/massif-api.json - the same schema the C++ property table is built from.

/* eslint-disable @typescript-eslint/no-unused-vars */

/**
 * Types for the MassifMaps surface API.
 *
 * Nothing here exists at runtime: the file compiles to an empty module. Every type is derived
 * from the SDK's own property table, so a path that completes here is a path the C++ resolves,
 * and a value the compiler rejects is one the SDK would have dropped with a warning.
 */

export type Json = null | boolean | number | string | Json[] | { [key: string]: Json };

/** `[lon, lat]` or `[lon, lat, altitude]`, in the object's own projection unless one is named. */
export type Position = [number, number] | [number, number, number];
/** A `[min, max]` pair of positions. */
export type Bounds = [Position, Position];
/** `[x, y, zoom]`. */
export type Tile = [number, number, number];
export interface ClickInfo {
    clickType: number;
}

/**
 * A registered object, as the C++ side numbers it.
 *
 * The phantom member is erased at runtime and exists so a source handle cannot be passed where a
 * layer handle belongs. It is INVARIANT in the class - a `Handle<PolygonGeometry>` is not a
 * `Handle<Geometry>` - which is why an OBJECT property's value type is a bare `Handle`: the
 * SDK downcasts a written handle and refuses the wrong class at runtime, so a brand there would
 * only reject the subclasses it actually accepts.
 *
 * The default is `any`, so a bare `Handle` means "some object" rather than "every class at
 * once", which nothing would satisfy.
 */
export type Handle<C extends ClassName = any> = number & { readonly __massif: C };

/** A well-known projection name, or any name registered with the SDK. */
export type ProjectionName = 'EPSG:4326' | 'EPSG:3857' | (string & {});

export type ClassName =
    | 'massif::Address'
    | 'massif::AnimationStyle'
    | 'massif::AnimationStyleBuilder'
    | 'massif::AssetPackage'
    | 'massif::AssetTileDataSource'
    | 'massif::BalloonPopup'
    | 'massif::BalloonPopupButton'
    | 'massif::BalloonPopupButtonClickInfo'
    | 'massif::BalloonPopupButtonStyle'
    | 'massif::BalloonPopupButtonStyleBuilder'
    | 'massif::BalloonPopupEventListener'
    | 'massif::BalloonPopupMargins'
    | 'massif::BalloonPopupStyle'
    | 'massif::BalloonPopupStyleBuilder'
    | 'massif::BaseMapView'
    | 'massif::Billboard'
    | 'massif::BillboardStyle'
    | 'massif::BillboardStyleBuilder'
    | 'massif::BinaryData'
    | 'massif::Bitmap'
    | 'massif::BitmapOverlayRasterTileDataSource'
    | 'massif::BundleAssetPackage'
    | 'massif::CacheTileDataSource'
    | 'massif::CartoCSSStyleSet'
    | 'massif::CelestialArc'
    | 'massif::CelestialClickInfo'
    | 'massif::CelestialEventListener'
    | 'massif::CelestialLabel'
    | 'massif::CelestialLayer'
    | 'massif::CelestialObject'
    | 'massif::CelestialSprite'
    | 'massif::ClickInfo'
    | 'massif::ClusterElementBuilder'
    | 'massif::ClusterFetchTask'
    | 'massif::ClusteredVectorLayer'
    | 'massif::Color'
    | 'massif::CombinedTileDataSource'
    | 'massif::CompiledStyleSet'
    | 'massif::CompositeVectorTileLayer'
    | 'massif::ContourTileDataSource'
    | 'massif::CullState'
    | 'massif::CustomPopup'
    | 'massif::CustomPopupHandler'
    | 'massif::CustomRasterTileLayer'
    | 'massif::DataSourceListener'
    | 'massif::DirAssetPackage'
    | 'massif::DouglasPeuckerGeometrySimplifier'
    | 'massif::DownloadTask'
    | 'massif::EPSG3857'
    | 'massif::EPSG4326'
    | 'massif::EditableVectorLayer'
    | 'massif::ElevationDecoder'
    | 'massif::EventListener'
    | 'massif::Feature'
    | 'massif::FeatureBuilder'
    | 'massif::FeatureCollection'
    | 'massif::FeatureCollectionSearchService'
    | 'massif::FetchTask'
    | 'massif::FetchTaskBase'
    | 'massif::FetchingTasks'
    | 'massif::FetchingTileTasks'
    | 'massif::FogOptions'
    | 'massif::GeoJSONGeometryReader'
    | 'massif::GeoJSONGeometryWriter'
    | 'massif::GeoJSONVectorTileDataSource'
    | 'massif::GeocodingAddress'
    | 'massif::GeocodingRequest'
    | 'massif::GeocodingResult'
    | 'massif::GeocodingService'
    | 'massif::Geometry'
    | 'massif::GeometryCollection'
    | 'massif::GeometryCollectionStyle'
    | 'massif::GeometryCollectionStyleBuilder'
    | 'massif::GeometrySimplifier'
    | 'massif::HTTPTileDataSource'
    | 'massif::HillshadeRasterTileLayer'
    | 'massif::Label'
    | 'massif::LabelStyle'
    | 'massif::LabelStyleBuilder'
    | 'massif::Layer'
    | 'massif::Layers'
    | 'massif::LightOptions'
    | 'massif::LightStop'
    | 'massif::Line'
    | 'massif::LineGeometry'
    | 'massif::LineStyle'
    | 'massif::LineStyleBuilder'
    | 'massif::LocalVectorDataSource'
    | 'massif::Log'
    | 'massif::LogEventListener'
    | 'massif::MBTilesTileDataSource'
    | 'massif::MBVectorTileDecoder'
    | 'massif::ManeuverArrowBuilder'
    | 'massif::MapBounds'
    | 'massif::MapBoxElevationDataDecoder'
    | 'massif::MapBoxOnlineGeocodingService'
    | 'massif::MapBoxOnlineReverseGeocodingService'
    | 'massif::MapClickInfo'
    | 'massif::MapEnvelope'
    | 'massif::MapEventListener'
    | 'massif::MapInteractionInfo'
    | 'massif::MapMoveInfo'
    | 'massif::MapPos'
    | 'massif::MapRange'
    | 'massif::MapRenderer'
    | 'massif::MapRendererListener'
    | 'massif::MapTile'
    | 'massif::MapTilerOnlineTileDataSource'
    | 'massif::MapVec'
    | 'massif::Marker'
    | 'massif::MarkerStyle'
    | 'massif::MarkerStyleBuilder'
    | 'massif::MassifApi'
    | 'massif::MassifInterop'
    | 'massif::MemoryCacheTileDataSource'
    | 'massif::MergedMBVTTileDataSource'
    | 'massif::MultiGeometry'
    | 'massif::MultiLineGeometry'
    | 'massif::MultiOSMOfflineGeocodingService'
    | 'massif::MultiOSMOfflineReverseGeocodingService'
    | 'massif::MultiPointGeometry'
    | 'massif::MultiPolygonGeometry'
    | 'massif::MultiTileDataSource'
    | 'massif::MultiValhallaOfflineRoutingService'
    | 'massif::NMLModel'
    | 'massif::NMLModelStyle'
    | 'massif::NMLModelStyleBuilder'
    | 'massif::OSMOfflineGeocodingService'
    | 'massif::OSMOfflineReverseGeocodingService'
    | 'massif::OSRMOfflineRoutingService'
    | 'massif::OnChangeListener'
    | 'massif::Options'
    | 'massif::OptionsListener'
    | 'massif::OrderedTileDataSource'
    | 'massif::PMTilesTileDataSource'
    | 'massif::PackageInfo'
    | 'massif::PackageManager'
    | 'massif::PackageManagerGeocodingService'
    | 'massif::PackageManagerListener'
    | 'massif::PackageManagerReverseGeocodingService'
    | 'massif::PackageManagerRoutingService'
    | 'massif::PackageManagerTileDataSource'
    | 'massif::PackageManagerValhallaRoutingService'
    | 'massif::PackageMetaInfo'
    | 'massif::PackageStatus'
    | 'massif::PackageTileMask'
    | 'massif::PeliasOnlineGeocodingService'
    | 'massif::PeliasOnlineReverseGeocodingService'
    | 'massif::PersistentCacheTileDataSource'
    | 'massif::PersistentTaskQueue'
    | 'massif::Point'
    | 'massif::PointDetailTileDataSource'
    | 'massif::PointGeometry'
    | 'massif::PointStyle'
    | 'massif::PointStyleBuilder'
    | 'massif::Polygon'
    | 'massif::Polygon3D'
    | 'massif::Polygon3DStyle'
    | 'massif::Polygon3DStyleBuilder'
    | 'massif::PolygonGeometry'
    | 'massif::PolygonStyle'
    | 'massif::PolygonStyleBuilder'
    | 'massif::Popup'
    | 'massif::PopupClickInfo'
    | 'massif::PopupDrawInfo'
    | 'massif::PopupStyle'
    | 'massif::PopupStyleBuilder'
    | 'massif::PostProcessEffect'
    | 'massif::Projection'
    | 'massif::RasterTileClickInfo'
    | 'massif::RasterTileEventListener'
    | 'massif::RasterTileLayer'
    | 'massif::RedrawRequestListener'
    | 'massif::RendererCaptureListener'
    | 'massif::ReverseGeocodingRequest'
    | 'massif::ReverseGeocodingService'
    | 'massif::RouteMatchingEdge'
    | 'massif::RouteMatchingPoint'
    | 'massif::RouteMatchingRequest'
    | 'massif::RouteMatchingResult'
    | 'massif::RoutingInstruction'
    | 'massif::RoutingRequest'
    | 'massif::RoutingResult'
    | 'massif::RoutingService'
    | 'massif::SGREOfflineRoutingService'
    | 'massif::ScreenBounds'
    | 'massif::ScreenPos'
    | 'massif::SearchRequest'
    | 'massif::SkyOptions'
    | 'massif::SolidLayer'
    | 'massif::Style'
    | 'massif::StyleBuilder'
    | 'massif::TerrainOptions'
    | 'massif::TerrariumElevationDataDecoder'
    | 'massif::Text'
    | 'massif::TextMargins'
    | 'massif::TextStyle'
    | 'massif::TextStyleBuilder'
    | 'massif::TileData'
    | 'massif::TileDataSource'
    | 'massif::TileDecoderListener'
    | 'massif::TileDownloadInfo'
    | 'massif::TileDownloadListener'
    | 'massif::TileInfo'
    | 'massif::TileLayer'
    | 'massif::TileLoadListener'
    | 'massif::TileUtils'
    | 'massif::TomTomOnlineGeocodingService'
    | 'massif::TomTomOnlineReverseGeocodingService'
    | 'massif::TorqueTileDecoder'
    | 'massif::TorqueTileLayer'
    | 'massif::TouchHandlerListener'
    | 'massif::UTFGridClickInfo'
    | 'massif::UTFGridEventListener'
    | 'massif::UiDispatcher'
    | 'massif::ValhallaOfflineRoutingService'
    | 'massif::ValhallaOnlineRoutingService'
    | 'massif::Variant'
    | 'massif::VariantArrayBuilder'
    | 'massif::VariantObjectBuilder'
    | 'massif::VectorData'
    | 'massif::VectorDataSource'
    | 'massif::VectorEditEventListener'
    | 'massif::VectorElement'
    | 'massif::VectorElementClickInfo'
    | 'massif::VectorElementDragInfo'
    | 'massif::VectorElementEventListener'
    | 'massif::VectorElementSearchService'
    | 'massif::VectorLayer'
    | 'massif::VectorTileClickInfo'
    | 'massif::VectorTileDecoder'
    | 'massif::VectorTileEventListener'
    | 'massif::VectorTileFeature'
    | 'massif::VectorTileFeatureBuilder'
    | 'massif::VectorTileFeatureCollection'
    | 'massif::VectorTileLayer'
    | 'massif::VectorTileSearchService'
    | 'massif::ViewState'
    | 'massif::WKBGeometryReader'
    | 'massif::WKBGeometryWriter'
    | 'massif::WKTGeometryReader'
    | 'massif::WKTGeometryWriter'
    | 'massif::ZippedAssetPackage'
    ;

// --- enums ---------------------------------------------------------------
//
// A string union of the constant names. The plugin translates a name to the int
// the C++ stores, in both directions, so an app never handles the number.

export type AnimationType =
    /** No animation is applied. */
    | 'ANIMATION_TYPE_NONE'
    /** Step transition is applied at the middle of the animation. */
    | 'ANIMATION_TYPE_STEP'
    /** Linear transition is used for animation. */
    | 'ANIMATION_TYPE_LINEAR'
    /** Smooth transition with 2nd order continuity is used for animation. */
    | 'ANIMATION_TYPE_SMOOTHSTEP'
    /** Spring-like transition is used for animation. */
    | 'ANIMATION_TYPE_SPRING'
    ;

export type BillboardOrientation =
    /** Billboard always faces the camera plane, regardless of rotation. */
    | 'BILLBOARD_ORIENTATION_FACE_CAMERA'
    /** Billboard lies parallel to the ground and rotates around it's anchor point to face the camera. SetRotation can be used to offset the final angle of the billboard. */
    | 'BILLBOARD_ORIENTATION_FACE_CAMERA_GROUND'
    /** Billboard lies on the ground, orientation does not depend on the camera position. */
    | 'BILLBOARD_ORIENTATION_GROUND'
    ;

export type BillboardScaling =
    /** Billboard has a constant world space size. Zooming causes the billboard to get smaller or bigger on the screen. */
    | 'BILLBOARD_SCALING_WORLD_SIZE'
    /** Billboard's size on screen is unaffected by zooming, but billboards that are further away from the camera get smaller when the tilt angle is < 90. */
    | 'BILLBOARD_SCALING_SCREEN_SIZE'
    /** Billboard's screen size is always the same, regardless of the zoom level, tilt angle or billboard's position. */
    | 'BILLBOARD_SCALING_CONST_SCREEN_SIZE'
    ;

export type ClickType =
    /** A click caused by pressing down and then releasing the screen. */
    | 'CLICK_TYPE_SINGLE'
    /** A click caused by pressing down but not releasing the screen. */
    | 'CLICK_TYPE_LONG'
    /** A click caused by two fast consecutive taps on the screen. */
    | 'CLICK_TYPE_DOUBLE'
    /** A click caused by two simultaneous taps on the screen. */
    | 'CLICK_TYPE_DUAL'
    ;

export type ClusterBuilderMode =
    /** Cluster builder receives full list of elements in the cluster. This mode is more expensive compared to the light mode. */
    | 'CLUSTER_BUILDER_MODE_ELEMENTS'
    /** Cluster builder receives element count in the cluster. This mode is less expensive compared to the full mode. */
    | 'CLUSTER_BUILDER_MODE_ELEMENT_COUNT'
    ;

export type ColorFormat =
    /** Options for identifiny unsupported image formats. */
    | 'COLOR_FORMAT_UNSUPPORTED'
    /** An image format that describes images with four channels, one for each color: blue, green and red and one for alpha. This color format will be converted to RGBA. */
    | 'COLOR_FORMAT_BGRA'
    /** An image format that describes images with four channels, one for each color: red, green, and blue and one for alpha. Each color is only four bits. This color format will be converted to RGBA. */
    | 'COLOR_FORMAT_RGBA_4444'
    /** An image format that describes images with three channels, one for each color: red, green, and blue. Red and blue colors are each packed into 5 bits, green into 6 bits. This color format will be converted to RGB. */
    | 'COLOR_FORMAT_RGB_565'
    ;

export type CompositeSourceType =
    /** A raster tile source, drawn as a RasterTileLayer at its style slot. */
    | 'COMPOSITE_SOURCE_TYPE_RASTER'
    /** An RGB-encoded elevation source, drawn as a HillshadeRasterTileLayer at its style slot. */
    | 'COMPOSITE_SOURCE_TYPE_HILLSHADE'
    /** Another MBVT/protobuf source (including ContourTileDataSource), drawn at its style slot as its own child VectorTileLayer with the master decoder, so it overzooms independently via its own MaxOverzoomLevel. */
    | 'COMPOSITE_SOURCE_TYPE_VECTOR'
    ;

export type FlightEasing =
    /** cubic-bezier(0.25, 0.1, 0.25, 1), the CSS "ease". The default, and mapbox-gl's. */
    | 'FLIGHT_EASING_EASE'
    /** No easing: the constant speed Van Wijk prescribes. Starts and stops abruptly. */
    | 'FLIGHT_EASING_LINEAR'
    /** cubic-bezier(0.42, 0, 1, 1). Gentle start, arrives at full speed. */
    | 'FLIGHT_EASING_EASE_IN'
    /** cubic-bezier(0, 0, 0.58, 1). Starts at full speed, comes to a stop. */
    | 'FLIGHT_EASING_EASE_OUT'
    /** cubic-bezier(0.42, 0, 0.58, 1). Symmetric, stronger than FLIGHT_EASING_EASE. */
    | 'FLIGHT_EASING_EASE_IN_OUT'
    ;

export type FreeRoamMode =
    /** Off: the standard map gestures. A one-finger drag pans the map. */
    | 'FREE_ROAM_MODE_OFF'
    /** Look: a one-finger drag turns the heading (sideways) and tilts (up/down) instead of panning; the camera still orbits its focus point. Panning moves to a two-finger drag; pinch and two-finger rotation are unchanged. */
    | 'FREE_ROAM_MODE_LOOK'
    /** First person: a one-finger drag turns the view about the camera, whose position never changes; a two-finger drag moves forward/back and strafes. Pinch and two-finger rotation are off. setTilt and setMapRotation also turn the view in place, so an orientation-driven camera matches the drag. */
    | 'FREE_ROAM_MODE_FIRST_PERSON'
    ;

export type GeometryType =
    /** A single position. */
    | 'GEOMETRY_TYPE_POINT'
    /** An ordered list of positions. */
    | 'GEOMETRY_TYPE_LINE'
    /** An outer ring and any number of holes. */
    | 'GEOMETRY_TYPE_POLYGON'
    /** Several points. */
    | 'GEOMETRY_TYPE_MULTIPOINT'
    /** Several lines. */
    | 'GEOMETRY_TYPE_MULTILINE'
    /** Several polygons. */
    | 'GEOMETRY_TYPE_MULTIPOLYGON'
    /** A mixed collection. */
    | 'GEOMETRY_TYPE_COLLECTION'
    ;

export type HillshadeMethod =
    /** MapLibre's legacy hillshade algorithm. */
    | 'STANDARD'
    /** Combined hillshade algorithm based on GDAL. */
    | 'COMBINED'
    /** Igor hillshade algorithm based on GDAL. */
    | 'IGOR'
    /** Multi-directional hillshade based on GDAL: four light sources at 225, 270, 315 and 360 degrees weighted by the aspect. Ignores the illumination azimuth, uses only its altitude. */
    | 'MULTIDIRECTIONAL'
    /** Basic hillshade algorithm based on GDAL. */
    | 'BASIC'
    ;

export type LineEndType =
    /** No line end points are drawn. */
    | 'LINE_END_TYPE_NONE'
    /** Line end points are drawn as squares. */
    | 'LINE_END_TYPE_SQUARE'
    /** Line end points are drawn as half circles. */
    | 'LINE_END_TYPE_ROUND'
    ;

export type LineJoinType =
    /** Line segments are not connected with each other. The fastest and ugliest. */
    | 'LINE_JOIN_TYPE_NONE'
    /** Line segments are connected with each other using miter connections. This is the preferred mode (fast and good looking generally). */
    | 'LINE_JOIN_TYPE_MITER'
    /** Line segments are connected with each other using bevel (straight line) connectors. Fast but results in an unnatural line. */
    | 'LINE_JOIN_TYPE_BEVEL'
    /** Line segments are connected with each other using circle sectors resulting in round corners. Slowest and prettiest. */
    | 'LINE_JOIN_TYPE_ROUND'
    ;

export type LocalSpatialIndexType =
    /** Null index, fastest if few elements are used. No element culling is performed. */
    | 'LOCAL_SPATIAL_INDEX_TYPE_NULL'
    /** K-d tree index, element culling is exact and fast. */
    | 'LOCAL_SPATIAL_INDEX_TYPE_KDTREE'
    ;

export type MBTilesScheme =
    /** The default scheme. Vertical coordinate is not flipped. */
    | 'MBTILES_SCHEME_TMS'
    /** Alternative to TMS scheme. Vertical coordinate is flipped. */
    | 'MBTILES_SCHEME_XYZ'
    ;

export type MapMoveReason =
    /** The user, directly: a gesture, a mouse wheel, or the inertia that follows one. */
    | 'MAP_MOVE_REASON_GESTURE'
    /** An animation the SDK is stepping - a flight, or a move given a duration. The call that started it reported the reason it was made with; every frame after that is this one. */
    | 'MAP_MOVE_REASON_ANIMATION'
    /** The app, through a call that took effect immediately: setFocusPos, setZoom, an option change that moved the camera. */
    | 'MAP_MOVE_REASON_API'
    ;

export type PackageAction =
    /** Package is ready. */
    | 'PACKAGE_ACTION_READY'
    /** Package is waiting in the task queue. */
    | 'PACKAGE_ACTION_WAITING'
    /** Package is being downloaded. */
    | 'PACKAGE_ACTION_DOWNLOADING'
    /** Package data is being copied. */
    | 'PACKAGE_ACTION_COPYING'
    /** Package is being removed. */
    | 'PACKAGE_ACTION_REMOVING'
    ;

export type PackageErrorType =
    /** Internal or system error. */
    | 'PACKAGE_ERROR_TYPE_SYSTEM'
    /** Connection or network error. */
    | 'PACKAGE_ERROR_TYPE_CONNECTION'
    /** The number of downloaded packages exceeded subscription limit. */
    | 'PACKAGE_ERROR_TYPE_DOWNLOAD_LIMIT_EXCEEDED'
    /** The bounding box of the package contains too many tiles. This error is only returned for custom bounding box packages. */
    | 'PACKAGE_ERROR_TYPE_PACKAGE_TOO_BIG'
    /** The license does not allow downloading offline packages. */
    | 'PACKAGE_ERROR_TYPE_NO_OFFLINE_PLAN'
    ;

export type PackageTileStatus =
    /** Tile is not part of the package. */
    | 'PACKAGE_TILE_STATUS_MISSING'
    /** Tile is part of the package, but package does not fully cover it. This value is no longer used, thus this is deprecated. */
    | 'PACKAGE_TILE_STATUS_PARTIAL'
    /** Tile if part of the package and package fully covers it. */
    | 'PACKAGE_TILE_STATUS_FULL'
    ;

export type PackageType =
    /** Map package. */
    | 'PACKAGE_TYPE_MAP'
    /** Routing package. */
    | 'PACKAGE_TYPE_ROUTING'
    /** Geocoding package. */
    | 'PACKAGE_TYPE_GEOCODING'
    /** Valhalla routing package. */
    | 'PACKAGE_TYPE_VALHALLA_ROUTING'
    ;

export type PanningMode =
    /** Free panning means that the map panning is unrestricted, user is able to zoom, rotate and pan the map at the same time without any artificial limits. */
    | 'PANNING_MODE_FREE'
    /** Sticky panning means that the map panning is restricted, user is able to freely pan the map, but zooming and rotating gestures can't be performed at the same time. User is still able to switch between zooming and rotating the map but it takes a bit more effort compared to FREE panning. */
    | 'PANNING_MODE_STICKY'
    /** Final sticky panning: like sticky panning, but once the gesture type is determined the user is stuck with either zooming or rotating until at least one of the two fingers is lifted. */
    | 'PANNING_MODE_STICKY_FINAL'
    ;

export type PanningSpeedMode =
    /** The map point under the finger follows it exactly. On a tilted view the speed then changes during the gesture, accelerating as the finger moves toward the far part of the screen. */
    | 'PANNING_SPEED_MODE_MAP'
    /** The scale is measured where the pan starts and stays fixed for the whole gesture, so the speed never changes while the finger is down. The default. */
    | 'PANNING_SPEED_MODE_ANCHORED'
    /** The scale is measured at the centre of the screen, so it depends neither on where the finger started nor on where it goes - every pan moves the map at the same rate. */
    | 'PANNING_SPEED_MODE_CONSTANT'
    ;

export type PivotMode =
    /** The touch point (or middle point between 2 finger touches) is used as the pivot point. */
    | 'PIVOT_MODE_TOUCHPOINT'
    /** Screen center is always used for pivot point. */
    | 'PIVOT_MODE_CENTERPOINT'
    ;

export type RasterTileFilterMode =
    /** No filter (nearest texel). */
    | 'RASTER_TILE_FILTER_MODE_NEAREST'
    /** Bilinear filter (interpolate between 4 closest texels). */
    | 'RASTER_TILE_FILTER_MODE_BILINEAR'
    /** Bicubic filter (interpolate between 16 closest texels). */
    | 'RASTER_TILE_FILTER_MODE_BICUBIC'
    ;

export type RenderProjectionMode =
    /** Planar projection. */
    | 'RENDER_PROJECTION_MODE_PLANAR'
    /** Spherical projection. */
    | 'RENDER_PROJECTION_MODE_SPHERICAL'
    ;

export type RouteMatchingPointType =
    /** The point was unmatched. */
    | 'ROUTE_MATCHING_POINT_UNMATCHED'
    /** The point was interpolated. */
    | 'ROUTE_MATCHING_POINT_INTERPOLATED'
    /** The point was matched. */
    | 'ROUTE_MATCHING_POINT_MATCHED'
    ;

export type RoutingAction =
    /** Head on, start the route. */
    | 'ROUTING_ACTION_HEAD_ON'
    /** Finish the route. */
    | 'ROUTING_ACTION_FINISH'
    /** Continue along the given street, do not turn. */
    | 'ROUTING_ACTION_NO_TURN'
    /** Go straight. */
    | 'ROUTING_ACTION_GO_STRAIGHT'
    /** Turn right. */
    | 'ROUTING_ACTION_TURN_RIGHT'
    /** Do an u-turn. */
    | 'ROUTING_ACTION_UTURN'
    /** Turn left. */
    | 'ROUTING_ACTION_TURN_LEFT'
    /** Reached given via point. If this is the final point, FINISH action is used instead. */
    | 'ROUTING_ACTION_REACH_VIA_LOCATION'
    /** Enter roundabout. Used by Valhalla and OSRM. */
    | 'ROUTING_ACTION_ENTER_ROUNDABOUT'
    /** Leave roundabout. Used by Valhalla and OSRM. */
    | 'ROUTING_ACTION_LEAVE_ROUNDABOUT'
    /** Continue along the roundabout. Only used by OSRM. */
    | 'ROUTING_ACTION_STAY_ON_ROUNDABOUT'
    /** Start at the end of the street. Currently used only by OSRM. */
    | 'ROUTING_ACTION_START_AT_END_OF_STREET'
    /** Enter street while moving against the allowed direction. Only used by OSRM. */
    | 'ROUTING_ACTION_ENTER_AGAINST_ALLOWED_DIRECTION'
    /** Leave the street while moving aginst the allowed direction. Only used by OSRM. */
    | 'ROUTING_ACTION_LEAVE_AGAINST_ALLOWED_DIRECTION'
    /** Go up. Only used by the SGRE. */
    | 'ROUTING_ACTION_GO_UP'
    /** Go down. Only used by SGRE. */
    | 'ROUTING_ACTION_GO_DOWN'
    /** Wait. Only used by SGRE. */
    | 'ROUTING_ACTION_WAIT'
    /** Enter ferry. Only used by Valhalla. */
    | 'ROUTING_ACTION_ENTER_FERRY'
    /** Leave ferry. Only used by Valhalla. */
    | 'ROUTING_ACTION_LEAVE_FERRY'
    ;

export type SkyQuality =
    /** 5 samples along the view ray, 3 towards the sun. */
    | 'SKY_QUALITY_LOW'
    /** 8 and 4. The default. */
    | 'SKY_QUALITY_MEDIUM'
    /** 12 and 5. */
    | 'SKY_QUALITY_HIGH'
    ;

export type SkyType =
    /** A gradient between HorizonColor and SkyColor; pick it for a flat, stylised or brand-coloured sky. */
    | 'SKY_TYPE_GRADIENT'
    /** Rayleigh and Mie single scattering, integrated along the view ray, so the sky follows the time of day on its own. */
    | 'SKY_TYPE_ATMOSPHERE'
    ;

export type TerrainFlattenMode =
    /** Rendering only: the terrain passes, the drape and the elevation fetches are dropped, but the tiles keep their terrain subdivision. Switching is instant, but a flat map carries 3D triangles. */
    | 'TERRAIN_FLATTEN_MODE_RENDER'
    /** The whole way: a flat map decodes, culls and draws as if no terrain were configured. The price is a re-decode at each switch, paid while the map is already flat. */
    | 'TERRAIN_FLATTEN_MODE_FULL'
    ;

export type TileFormat =
    /** Detect the format from the tile data. The two are unambiguous in practice, but set the format explicitly when the source is known - it skips the check and cannot be fooled. */
    | 'TILE_FORMAT_AUTO'
    /** MapBox Vector Tile, the protobuf format. */
    | 'TILE_FORMAT_MVT'
    /** MapLibre Tile, the columnar format. Smaller tiles and faster decoding, but the whole tile is decoded at once - MVT decodes only the layers and attributes the style asks for. */
    | 'TILE_FORMAT_MLT'
    ;

export type TileLODProfile =
    /** The reference density: TileLODFactor 1, the tangram/mapbox/maplibre rule - a tile is refined while it covers more than a 2x2 block of nominal tiles. Fewest tiles. */
    | 'TILE_LOD_PROFILE_REFERENCE'
    /** Half a level finer than the reference, with a shorter style zoom lift. Meant for a phone: visibly sharper than the reference at roughly twice its tile count. */
    | 'TILE_LOD_PROFILE_MOBILE'
    /** A full level finer than the reference (TileLODFactor 0.5, the historical default), about 4x its tile count. Meant for a desktop or a web page on a real GPU. */
    | 'TILE_LOD_PROFILE_DESKTOP'
    ;

export type TileSubstitutionPolicy =
    /** Consider all cached/loaded tiles. */
    | 'TILE_SUBSTITUTION_POLICY_ALL'
    /** Consider only tiles that are currently visible. This is recommended for low-latency data sources, like offline sources. */
    | 'TILE_SUBSTITUTION_POLICY_VISIBLE'
    /** Never substitute tiles. */
    | 'TILE_SUBSTITUTION_POLICY_NONE'
    ;

export type VariantType =
    /** Null element. */
    | 'VARIANT_TYPE_NULL'
    /** String element. */
    | 'VARIANT_TYPE_STRING'
    /** Boolean element. */
    | 'VARIANT_TYPE_BOOL'
    /** Integer element. */
    | 'VARIANT_TYPE_INTEGER'
    /** Double-precision floating point element. */
    | 'VARIANT_TYPE_DOUBLE'
    /** Array element. */
    | 'VARIANT_TYPE_ARRAY'
    /** Object (dictionary) element. */
    | 'VARIANT_TYPE_OBJECT'
    ;

export type VectorElementDragMode =
    /** A single vertex is being moved. */
    | 'VECTOR_ELEMENT_DRAG_MODE_VERTEX'
    /** The whole vector element is being moved. */
    | 'VECTOR_ELEMENT_DRAG_MODE_ELEMENT'
    ;

export type VectorElementDragPointStyle =
    /** Normal control point. Corresponds to vertex. */
    | 'VECTOR_ELEMENT_DRAG_POINT_STYLE_NORMAL'
    /** Virtual control point (midpoint between actual vertices). Used for lines and polygons. */
    | 'VECTOR_ELEMENT_DRAG_POINT_STYLE_VIRTUAL'
    /** Selected control point. */
    | 'VECTOR_ELEMENT_DRAG_POINT_STYLE_SELECTED'
    ;

export type VectorElementDragResult =
    /** Dragging should be ignored. The input event will be passed on to other handlers. */
    | 'VECTOR_ELEMENT_DRAG_RESULT_IGNORE'
    /** Dragging should be ignored. The input event is not passed on to other handlers. */
    | 'VECTOR_ELEMENT_DRAG_RESULT_STOP'
    /** The underlying vector element (or vertex) should be modified. */
    | 'VECTOR_ELEMENT_DRAG_RESULT_MODIFY'
    /** The underlying vector element or vertex should be deleted. */
    | 'VECTOR_ELEMENT_DRAG_RESULT_DELETE'
    ;

export type VectorTileRenderOrder =
    /** No rendering, elements are hidden. */
    | 'VECTOR_TILE_RENDER_ORDER_HIDDEN'
    /** Elements are rendered together with the same layer elements. Layers that are on top of the layers are rendered on top this layer. */
    | 'VECTOR_TILE_RENDER_ORDER_LAYER'
    /** Elements are rendered on top of all normal layers. */
    | 'VECTOR_TILE_RENDER_ORDER_LAST'
    ;

export type massifChangeType =
    | 'PACKAGES_ADDED'
    | 'PACKAGES_DELETED'
    ;

export type massifCommand =
    | 'NOP'
    | 'DOWNLOAD_PACKAGE_LIST'
    | 'DOWNLOAD_PACKAGE'
    | 'IMPORT_PACKAGE'
    | 'REMOVE_PACKAGE'
    | 'DOWNLOAD_STYLE'
    ;

export type massifPackageChangeType =
    | 'PACKAGES_UPDATED'
    | 'PACKAGES_ADDED'
    | 'PACKAGES_DELETED'
    ;

// --- properties ----------------------------------------------------------
//
// One entry per class: every path reachable from it, and the type at the end.

/** @internal A lookup table, not documentation - see Path and ValueAt. */
export interface PropertyTypes {
    'massif::Address': {
        /** (read-only) Returns the list of category tags describing the address. */
        readonly 'categories': string[];
        /** (read-only) Returns the country name included in the address. */
        readonly 'country': string;
        /** (read-only) Returns the county name included in the address. */
        readonly 'county': string;
        /** (read-only) Returns the house number included in the address. */
        readonly 'houseNumber': string;
        /** (read-only) Returns the locality (city, town, village) name included in the address. */
        readonly 'locality': string;
        /** (read-only) Returns the name included in the address. */
        readonly 'name': string;
        /** (read-only) Returns the local neighbourhood name included in the address. */
        readonly 'neighbourhood': string;
        /** (read-only) Returns the postcode of the address. */
        readonly 'postcode': string;
        /** (read-only) Returns the region name included in the address. */
        readonly 'region': string;
        /** (read-only) Returns the street name included in the address. */
        readonly 'street': string;
    };
    'massif::AnimationStyle': {
        /** (read-only) Returns the fade animation type. */
        readonly 'fadeAnimationType': 'ANIMATION_TYPE_NONE' | 'ANIMATION_TYPE_STEP' | 'ANIMATION_TYPE_LINEAR' | 'ANIMATION_TYPE_SMOOTHSTEP' | 'ANIMATION_TYPE_SPRING' | number;
        /** (read-only) Returns the phase-in duration of the animation. */
        readonly 'phaseInDuration': number;
        /** (read-only) Returns the phase-out duration of the animation. */
        readonly 'phaseOutDuration': number;
        /** (read-only) Returns the relative speed of the animation. */
        readonly 'relativeSpeed': number;
        /** (read-only) Returns the size-related animation type. */
        readonly 'sizeAnimationType': 'ANIMATION_TYPE_NONE' | 'ANIMATION_TYPE_STEP' | 'ANIMATION_TYPE_LINEAR' | 'ANIMATION_TYPE_SMOOTHSTEP' | 'ANIMATION_TYPE_SPRING' | number;
    };
    'massif::AnimationStyleBuilder': {
        /** Returns the fade animation type. */
        'fadeAnimationType': 'ANIMATION_TYPE_NONE' | 'ANIMATION_TYPE_STEP' | 'ANIMATION_TYPE_LINEAR' | 'ANIMATION_TYPE_SMOOTHSTEP' | 'ANIMATION_TYPE_SPRING' | number;
        /** Returns the phase-in duration of the animation. */
        'phaseInDuration': number;
        /** Returns the phase-out duration of the animation. */
        'phaseOutDuration': number;
        /** Returns the relative speed of the animation. */
        'relativeSpeed': number;
        /** Returns the size-related animation type. */
        'sizeAnimationType': 'ANIMATION_TYPE_NONE' | 'ANIMATION_TYPE_STEP' | 'ANIMATION_TYPE_LINEAR' | 'ANIMATION_TYPE_SMOOTHSTEP' | 'ANIMATION_TYPE_SPRING' | number;
    };
    'massif::AssetPackage': {
        readonly 'assetNames': string[];
    };
    'massif::AssetTileDataSource': {
        /** (read-only) Returns the extent of the tiles in this data source. The bounds are in coordinate system of the projection of the data source. */
        readonly 'dataExtent': Bounds;
        /** Gets the current maximum overzoom level for this datasource. Over it the datasource will not be "drawn" */
        'maxOverzoomLevel': number;
        /** (read-only) Returns the maximum zoom level supported by this data source. */
        readonly 'maxZoom': number;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        'metaData': Record<string, Json>;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        [key: `metaData.${string}`]: Json;
        /** (read-only) Returns the minimum zoom level supported by this data source. */
        readonly 'minZoom': number;
        /** (read-only) Returns the projection of this tile source. */
        readonly 'projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'projection.bounds': Bounds;
        readonly 'projection.name': string;
    };
    'massif::BalloonPopup': {
        /** Returns the horizontal anchor point of this popup. */
        'anchorPointX': number;
        /** Returns the vertical anchor point of this popup. */
        'anchorPointY': number;
        /** Returns the balloon popup event listener. */
        'balloonPopupEventListener': Handle;
        /** Returns the base billboard this billboard is attached to. */
        'baseBillboard': Handle;
        /** Returns the base billboard this billboard is attached to. */
        'baseBillboard.baseBillboard': Handle;
        /** (read-only) Returns the bounds of this billboard or the base billboard, if there is one. */
        readonly 'baseBillboard.bounds': Bounds;
        /** Returns the geometry object that defines the location of this billboard. */
        'baseBillboard.geometry': Handle;
        /** (read-only) Returns the minimal bounds for the geometry. */
        readonly 'baseBillboard.geometry.bounds': Bounds;
        readonly 'baseBillboard.geometry.centerPos': Position;
        /** (read-only) Returns the geometry as a GeoJSON string, in its own coordinates. Here rather than only on Feature because serialising a shape otherwise means constructing a GeoJSONGeometryWriter, which no string-based binding can do. */
        readonly 'baseBillboard.geometry.geoJSON': string;
        readonly 'baseBillboard.geometry.type': 'GEOMETRY_TYPE_POINT' | 'GEOMETRY_TYPE_LINE' | 'GEOMETRY_TYPE_POLYGON' | 'GEOMETRY_TYPE_MULTIPOINT' | 'GEOMETRY_TYPE_MULTILINE' | 'GEOMETRY_TYPE_MULTIPOLYGON' | 'GEOMETRY_TYPE_COLLECTION' | number;
        /** Returns the internal id of this vector element. */
        'baseBillboard.id': number;
        /** Returns a copy of the vector element meta data map. The changes you make to this map are NOT reflected in the actual meta data of the element. */
        'baseBillboard.metaData': Record<string, Json>;
        /** Returns a copy of the vector element meta data map. The changes you make to this map are NOT reflected in the actual meta data of the element. */
        [key: `baseBillboard.metaData.${string}`]: Json;
        /** (read-only) Returns the location of the root billboard: getGeometry() if this billboard has a location, otherwise the location found by following the chain of base billboards to its root. */
        readonly 'baseBillboard.rootGeometry': Handle;
        /** (read-only) Returns the minimal bounds for the geometry. */
        readonly 'baseBillboard.rootGeometry.bounds': Bounds;
        readonly 'baseBillboard.rootGeometry.centerPos': Position;
        /** (read-only) Returns the geometry as a GeoJSON string, in its own coordinates. Here rather than only on Feature because serialising a shape otherwise means constructing a GeoJSONGeometryWriter, which no string-based binding can do. */
        readonly 'baseBillboard.rootGeometry.geoJSON': string;
        readonly 'baseBillboard.rootGeometry.type': 'GEOMETRY_TYPE_POINT' | 'GEOMETRY_TYPE_LINE' | 'GEOMETRY_TYPE_POLYGON' | 'GEOMETRY_TYPE_MULTIPOINT' | 'GEOMETRY_TYPE_MULTILINE' | 'GEOMETRY_TYPE_MULTIPOLYGON' | 'GEOMETRY_TYPE_COLLECTION' | number;
        /** Returns the rotation angle of this billboard. */
        'baseBillboard.rotation': number;
        /** Returns the state of the visibility flag of this vector element. */
        'baseBillboard.visible': boolean;
        /** (read-only) Returns the bounds of this billboard or the base billboard, if there is one. */
        readonly 'bounds': Bounds;
        /** Returns the description of this balloon popup. */
        'description': string;
        /** Returns the geometry object that defines the location of this billboard. */
        'geometry': Handle;
        /** (read-only) Returns the minimal bounds for the geometry. */
        readonly 'geometry.bounds': Bounds;
        readonly 'geometry.centerPos': Position;
        /** (read-only) Returns the geometry as a GeoJSON string, in its own coordinates. Here rather than only on Feature because serialising a shape otherwise means constructing a GeoJSONGeometryWriter, which no string-based binding can do. */
        readonly 'geometry.geoJSON': string;
        readonly 'geometry.type': 'GEOMETRY_TYPE_POINT' | 'GEOMETRY_TYPE_LINE' | 'GEOMETRY_TYPE_POLYGON' | 'GEOMETRY_TYPE_MULTIPOINT' | 'GEOMETRY_TYPE_MULTILINE' | 'GEOMETRY_TYPE_MULTIPOLYGON' | 'GEOMETRY_TYPE_COLLECTION' | number;
        /** Returns the internal id of this vector element. */
        'id': number;
        /** Returns a copy of the vector element meta data map. The changes you make to this map are NOT reflected in the actual meta data of the element. */
        'metaData': Record<string, Json>;
        /** Returns a copy of the vector element meta data map. The changes you make to this map are NOT reflected in the actual meta data of the element. */
        [key: `metaData.${string}`]: Json;
        /** (read-only) Returns the location of the root billboard: getGeometry() if this billboard has a location, otherwise the location found by following the chain of base billboards to its root. */
        readonly 'rootGeometry': Handle;
        /** (read-only) Returns the minimal bounds for the geometry. */
        readonly 'rootGeometry.bounds': Bounds;
        readonly 'rootGeometry.centerPos': Position;
        /** (read-only) Returns the geometry as a GeoJSON string, in its own coordinates. Here rather than only on Feature because serialising a shape otherwise means constructing a GeoJSONGeometryWriter, which no string-based binding can do. */
        readonly 'rootGeometry.geoJSON': string;
        readonly 'rootGeometry.type': 'GEOMETRY_TYPE_POINT' | 'GEOMETRY_TYPE_LINE' | 'GEOMETRY_TYPE_POLYGON' | 'GEOMETRY_TYPE_MULTIPOINT' | 'GEOMETRY_TYPE_MULTILINE' | 'GEOMETRY_TYPE_MULTIPOLYGON' | 'GEOMETRY_TYPE_COLLECTION' | number;
        /** Returns the rotation angle of this billboard. */
        'rotation': number;
        /** Returns the style of this balloon popup. */
        'style': Handle;
        /** (read-only) Returns the animation style of the billboard. */
        readonly 'style.animationStyle': Handle;
        /** (read-only) Returns the fade animation type. */
        readonly 'style.animationStyle.fadeAnimationType': 'ANIMATION_TYPE_NONE' | 'ANIMATION_TYPE_STEP' | 'ANIMATION_TYPE_LINEAR' | 'ANIMATION_TYPE_SMOOTHSTEP' | 'ANIMATION_TYPE_SPRING' | number;
        /** (read-only) Returns the phase-in duration of the animation. */
        readonly 'style.animationStyle.phaseInDuration': number;
        /** (read-only) Returns the phase-out duration of the animation. */
        readonly 'style.animationStyle.phaseOutDuration': number;
        /** (read-only) Returns the relative speed of the animation. */
        readonly 'style.animationStyle.relativeSpeed': number;
        /** (read-only) Returns the size-related animation type. */
        readonly 'style.animationStyle.sizeAnimationType': 'ANIMATION_TYPE_NONE' | 'ANIMATION_TYPE_STEP' | 'ANIMATION_TYPE_LINEAR' | 'ANIMATION_TYPE_SMOOTHSTEP' | 'ANIMATION_TYPE_SPRING' | number;
        /** (read-only) Returns the horizontal attaching anchor point of the billoard. */
        readonly 'style.attachAnchorPointX': number;
        /** (read-only) Returns the vertical attaching anchor point of the billoard. */
        readonly 'style.attachAnchorPointY': number;
        /** (read-only) Returns the background color of the popup. */
        readonly 'style.backgroundColor': number;
        /** (read-only) Returns the margins for the buttons of the popup. */
        readonly 'style.buttonMargins': Json;
        /** (read-only) Returns the state of the causes overlap flag. */
        readonly 'style.causesOverlap': boolean;
        /** (read-only) Returns the color of the vector element. */
        readonly 'style.color': number;
        /** (read-only) Returns the corner radius of the popup. */
        readonly 'style.cornerRadius': number;
        /** (read-only) Returns the color of the description. */
        readonly 'style.descriptionColor': number;
        /** (read-only) Returns the description field variable to use. */
        readonly 'style.descriptionField': string;
        /** (read-only) Returns the name of the description font. */
        readonly 'style.descriptionFontName': string;
        /** (read-only) Returns the size of the description font. */
        readonly 'style.descriptionFontSize': number;
        /** (read-only) Returns the margins of the description. */
        readonly 'style.descriptionMargins': Json;
        /** (read-only) Returns the state of the description wrap parameter. */
        readonly 'style.descriptionWrap': boolean;
        /** (read-only) Returns the state of the allow overlap flag. */
        readonly 'style.hideIfOverlapped': boolean;
        /** (read-only) Returns the horizontal offset of the billboard. */
        readonly 'style.horizontalOffset': number;
        /** (read-only) Returns the background color of the left part of the popup. */
        readonly 'style.leftColor': number;
        /** (read-only) Returns the image of the left part of the popup. */
        readonly 'style.leftImage': Handle;
        /** (read-only) Returns the bytes per pixel parameter of this bitmap. Valid values are 1, 2, 3 and 4. */
        readonly 'style.leftImage.bytesPerPixel': number;
        /** (read-only) Returns the color format of this bitmap. */
        readonly 'style.leftImage.colorFormat': 'COLOR_FORMAT_UNSUPPORTED' | 'COLOR_FORMAT_BGRA' | 'COLOR_FORMAT_RGBA_4444' | 'COLOR_FORMAT_RGB_565' | number;
        /** (read-only) Returns the height of the bitmap. */
        readonly 'style.leftImage.height': number;
        /** (read-only) Returns the width of the bitmap. */
        readonly 'style.leftImage.width': number;
        /** (read-only) Returns the margins of the left part of the popup. */
        readonly 'style.leftMargins': Json;
        /** (read-only) Returns the placement priority of the billboard. */
        readonly 'style.placementPriority': number;
        /** (read-only) Returns the background color of the right part of the popup. */
        readonly 'style.rightColor': number;
        /** (read-only) Returns the image of the right part of the popup. */
        readonly 'style.rightImage': Handle;
        /** (read-only) Returns the bytes per pixel parameter of this bitmap. Valid values are 1, 2, 3 and 4. */
        readonly 'style.rightImage.bytesPerPixel': number;
        /** (read-only) Returns the color format of this bitmap. */
        readonly 'style.rightImage.colorFormat': 'COLOR_FORMAT_UNSUPPORTED' | 'COLOR_FORMAT_BGRA' | 'COLOR_FORMAT_RGBA_4444' | 'COLOR_FORMAT_RGB_565' | number;
        /** (read-only) Returns the height of the bitmap. */
        readonly 'style.rightImage.height': number;
        /** (read-only) Returns the width of the bitmap. */
        readonly 'style.rightImage.width': number;
        /** (read-only) Returns the margins of the right part of the popup. */
        readonly 'style.rightMargins': Json;
        /** (read-only) Returns the state of the scale with DPI flag. */
        readonly 'style.scaleWithDPI': boolean;
        /** (read-only) Returns the color of the stroke surrounding the popup. */
        readonly 'style.strokeColor': number;
        /** (read-only) Returns the width of the stroke surrounding the popup. */
        readonly 'style.strokeWidth': number;
        /** (read-only) Returns the color of the title. */
        readonly 'style.titleColor': number;
        /** (read-only) Returns the title field variable to use. */
        readonly 'style.titleField': string;
        /** (read-only) Returns the name of the title font. */
        readonly 'style.titleFontName': string;
        /** (read-only) Returns the size of the title font. */
        readonly 'style.titleFontSize': number;
        /** (read-only) Returns the margins of the title. */
        readonly 'style.titleMargins': Json;
        /** (read-only) Returns the state of the title wrap parameter. */
        readonly 'style.titleWrap': boolean;
        /** (read-only) Returns the height of the triangle at the bottom of the popup. */
        readonly 'style.triangleHeight': number;
        /** (read-only) Returns the width of the triangle at the bottom of the popup. */
        readonly 'style.triangleWidth': number;
        /** (read-only) Returns the vertical offset of the billboard. */
        readonly 'style.verticalOffset': number;
        /** Returns the title of this balloon popup. */
        'title': string;
        /** Returns the state of the visibility flag of this vector element. */
        'visible': boolean;
    };
    'massif::BalloonPopupButton': {
        /** (read-only) Returns the style of this button. */
        readonly 'style': Handle;
        /** (read-only) Returns the background color of the button. */
        readonly 'style.backgroundColor': number;
        /** (read-only) Returns the width of the button. If this value is -1, then button width is calculated automatically based on button text. */
        readonly 'style.buttonWidth': number;
        /** (read-only) Returns the color of the vector element. */
        readonly 'style.color': number;
        /** (read-only) Returns the corner radius of the button. */
        readonly 'style.cornerRadius': number;
        /** (read-only) Returns the color of the stroke surrounding the button. */
        readonly 'style.strokeColor': number;
        /** (read-only) Returns the width of the stroke surrounding the button. */
        readonly 'style.strokeWidth': number;
        /** (read-only) Returns the color of the text. */
        readonly 'style.textColor': number;
        /** (read-only) Returns the name of the text font. */
        readonly 'style.textFontName': string;
        /** (read-only) Returns the size of the text font. */
        readonly 'style.textFontSize': number;
        /** (read-only) Returns the margins of the text. */
        readonly 'style.textMargins': Json;
        /** Returns the user-defined tag associated with the button. */
        'tag': Json;
        /** (read-only) Returns the text of this button. */
        readonly 'text': string;
    };
    'massif::BalloonPopupButtonClickInfo': {
        /** (read-only) Returns the clicked button. */
        readonly 'button': Handle;
        /** (read-only) Returns the style of this button. */
        readonly 'button.style': Handle;
        /** (read-only) Returns the background color of the button. */
        readonly 'button.style.backgroundColor': number;
        /** (read-only) Returns the width of the button. If this value is -1, then button width is calculated automatically based on button text. */
        readonly 'button.style.buttonWidth': number;
        /** (read-only) Returns the color of the vector element. */
        readonly 'button.style.color': number;
        /** (read-only) Returns the corner radius of the button. */
        readonly 'button.style.cornerRadius': number;
        /** (read-only) Returns the color of the stroke surrounding the button. */
        readonly 'button.style.strokeColor': number;
        /** (read-only) Returns the width of the stroke surrounding the button. */
        readonly 'button.style.strokeWidth': number;
        /** (read-only) Returns the color of the text. */
        readonly 'button.style.textColor': number;
        /** (read-only) Returns the name of the text font. */
        readonly 'button.style.textFontName': string;
        /** (read-only) Returns the size of the text font. */
        readonly 'button.style.textFontSize': number;
        /** (read-only) Returns the margins of the text. */
        readonly 'button.style.textMargins': Json;
        /** Returns the user-defined tag associated with the button. */
        'button.tag': Json;
        /** (read-only) Returns the text of this button. */
        readonly 'button.text': string;
        /** (read-only) Returns the click info. */
        readonly 'clickInfo': ClickInfo;
        /** (read-only) Returns the click type. */
        readonly 'clickType': 'CLICK_TYPE_SINGLE' | 'CLICK_TYPE_LONG' | 'CLICK_TYPE_DOUBLE' | 'CLICK_TYPE_DUAL' | number;
        /** (read-only) Returns the clicked vector element. */
        readonly 'vectorElement': Handle;
        /** (read-only) Returns the bounds of this vector element. */
        readonly 'vectorElement.bounds': Bounds;
        /** (read-only) Returns the geometry object that defines the location of this vector element. */
        readonly 'vectorElement.geometry': Handle;
        /** (read-only) Returns the minimal bounds for the geometry. */
        readonly 'vectorElement.geometry.bounds': Bounds;
        readonly 'vectorElement.geometry.centerPos': Position;
        /** (read-only) Returns the geometry as a GeoJSON string, in its own coordinates. Here rather than only on Feature because serialising a shape otherwise means constructing a GeoJSONGeometryWriter, which no string-based binding can do. */
        readonly 'vectorElement.geometry.geoJSON': string;
        readonly 'vectorElement.geometry.type': 'GEOMETRY_TYPE_POINT' | 'GEOMETRY_TYPE_LINE' | 'GEOMETRY_TYPE_POLYGON' | 'GEOMETRY_TYPE_MULTIPOINT' | 'GEOMETRY_TYPE_MULTILINE' | 'GEOMETRY_TYPE_MULTIPOLYGON' | 'GEOMETRY_TYPE_COLLECTION' | number;
        /** Returns the internal id of this vector element. */
        'vectorElement.id': number;
        /** Returns a copy of the vector element meta data map. The changes you make to this map are NOT reflected in the actual meta data of the element. */
        'vectorElement.metaData': Record<string, Json>;
        /** Returns a copy of the vector element meta data map. The changes you make to this map are NOT reflected in the actual meta data of the element. */
        [key: `vectorElement.metaData.${string}`]: Json;
        /** Returns the state of the visibility flag of this vector element. */
        'vectorElement.visible': boolean;
    };
    'massif::BalloonPopupButtonStyle': {
        /** (read-only) Returns the background color of the button. */
        readonly 'backgroundColor': number;
        /** (read-only) Returns the width of the button. If this value is -1, then button width is calculated automatically based on button text. */
        readonly 'buttonWidth': number;
        /** (read-only) Returns the color of the vector element. */
        readonly 'color': number;
        /** (read-only) Returns the corner radius of the button. */
        readonly 'cornerRadius': number;
        /** (read-only) Returns the color of the stroke surrounding the button. */
        readonly 'strokeColor': number;
        /** (read-only) Returns the width of the stroke surrounding the button. */
        readonly 'strokeWidth': number;
        /** (read-only) Returns the color of the text. */
        readonly 'textColor': number;
        /** (read-only) Returns the name of the text font. */
        readonly 'textFontName': string;
        /** (read-only) Returns the size of the text font. */
        readonly 'textFontSize': number;
        /** (read-only) Returns the margins of the text. */
        readonly 'textMargins': Json;
    };
    'massif::BalloonPopupButtonStyleBuilder': {
        /** Returns the width of the button. If this value is -1, then button width is calculated automatically based on button text. */
        'buttonWidth': number;
        /** Returns the color of the vector element. */
        'color': number;
        /** Returns the corner radius of the button. */
        'cornerRadius': number;
        /** Returns the color of the stroke surrounding the button. */
        'strokeColor': number;
        /** Returns the width of the stroke surrounding the button. */
        'strokeWidth': number;
        /** Returns the color of the text. */
        'textColor': number;
        /** Returns the name of the text font. */
        'textFontName': string;
        /** Returns the size of the text font. */
        'textFontSize': number;
        /** Returns the margins of the text. */
        'textMargins': Json;
    };
    'massif::BalloonPopupEventListener': {
    };
    'massif::BalloonPopupMargins': {
        readonly 'bottom': number;
        readonly 'left': number;
        readonly 'right': number;
        readonly 'top': number;
    };
    'massif::BalloonPopupStyle': {
        /** (read-only) Returns the animation style of the billboard. */
        readonly 'animationStyle': Handle;
        /** (read-only) Returns the fade animation type. */
        readonly 'animationStyle.fadeAnimationType': 'ANIMATION_TYPE_NONE' | 'ANIMATION_TYPE_STEP' | 'ANIMATION_TYPE_LINEAR' | 'ANIMATION_TYPE_SMOOTHSTEP' | 'ANIMATION_TYPE_SPRING' | number;
        /** (read-only) Returns the phase-in duration of the animation. */
        readonly 'animationStyle.phaseInDuration': number;
        /** (read-only) Returns the phase-out duration of the animation. */
        readonly 'animationStyle.phaseOutDuration': number;
        /** (read-only) Returns the relative speed of the animation. */
        readonly 'animationStyle.relativeSpeed': number;
        /** (read-only) Returns the size-related animation type. */
        readonly 'animationStyle.sizeAnimationType': 'ANIMATION_TYPE_NONE' | 'ANIMATION_TYPE_STEP' | 'ANIMATION_TYPE_LINEAR' | 'ANIMATION_TYPE_SMOOTHSTEP' | 'ANIMATION_TYPE_SPRING' | number;
        /** (read-only) Returns the horizontal attaching anchor point of the billoard. */
        readonly 'attachAnchorPointX': number;
        /** (read-only) Returns the vertical attaching anchor point of the billoard. */
        readonly 'attachAnchorPointY': number;
        /** (read-only) Returns the background color of the popup. */
        readonly 'backgroundColor': number;
        /** (read-only) Returns the margins for the buttons of the popup. */
        readonly 'buttonMargins': Json;
        /** (read-only) Returns the state of the causes overlap flag. */
        readonly 'causesOverlap': boolean;
        /** (read-only) Returns the color of the vector element. */
        readonly 'color': number;
        /** (read-only) Returns the corner radius of the popup. */
        readonly 'cornerRadius': number;
        /** (read-only) Returns the color of the description. */
        readonly 'descriptionColor': number;
        /** (read-only) Returns the description field variable to use. */
        readonly 'descriptionField': string;
        /** (read-only) Returns the name of the description font. */
        readonly 'descriptionFontName': string;
        /** (read-only) Returns the size of the description font. */
        readonly 'descriptionFontSize': number;
        /** (read-only) Returns the margins of the description. */
        readonly 'descriptionMargins': Json;
        /** (read-only) Returns the state of the description wrap parameter. */
        readonly 'descriptionWrap': boolean;
        /** (read-only) Returns the state of the allow overlap flag. */
        readonly 'hideIfOverlapped': boolean;
        /** (read-only) Returns the horizontal offset of the billboard. */
        readonly 'horizontalOffset': number;
        /** (read-only) Returns the background color of the left part of the popup. */
        readonly 'leftColor': number;
        /** (read-only) Returns the image of the left part of the popup. */
        readonly 'leftImage': Handle;
        /** (read-only) Returns the bytes per pixel parameter of this bitmap. Valid values are 1, 2, 3 and 4. */
        readonly 'leftImage.bytesPerPixel': number;
        /** (read-only) Returns the color format of this bitmap. */
        readonly 'leftImage.colorFormat': 'COLOR_FORMAT_UNSUPPORTED' | 'COLOR_FORMAT_BGRA' | 'COLOR_FORMAT_RGBA_4444' | 'COLOR_FORMAT_RGB_565' | number;
        /** (read-only) Returns the height of the bitmap. */
        readonly 'leftImage.height': number;
        /** (read-only) Returns the width of the bitmap. */
        readonly 'leftImage.width': number;
        /** (read-only) Returns the margins of the left part of the popup. */
        readonly 'leftMargins': Json;
        /** (read-only) Returns the placement priority of the billboard. */
        readonly 'placementPriority': number;
        /** (read-only) Returns the background color of the right part of the popup. */
        readonly 'rightColor': number;
        /** (read-only) Returns the image of the right part of the popup. */
        readonly 'rightImage': Handle;
        /** (read-only) Returns the bytes per pixel parameter of this bitmap. Valid values are 1, 2, 3 and 4. */
        readonly 'rightImage.bytesPerPixel': number;
        /** (read-only) Returns the color format of this bitmap. */
        readonly 'rightImage.colorFormat': 'COLOR_FORMAT_UNSUPPORTED' | 'COLOR_FORMAT_BGRA' | 'COLOR_FORMAT_RGBA_4444' | 'COLOR_FORMAT_RGB_565' | number;
        /** (read-only) Returns the height of the bitmap. */
        readonly 'rightImage.height': number;
        /** (read-only) Returns the width of the bitmap. */
        readonly 'rightImage.width': number;
        /** (read-only) Returns the margins of the right part of the popup. */
        readonly 'rightMargins': Json;
        /** (read-only) Returns the state of the scale with DPI flag. */
        readonly 'scaleWithDPI': boolean;
        /** (read-only) Returns the color of the stroke surrounding the popup. */
        readonly 'strokeColor': number;
        /** (read-only) Returns the width of the stroke surrounding the popup. */
        readonly 'strokeWidth': number;
        /** (read-only) Returns the color of the title. */
        readonly 'titleColor': number;
        /** (read-only) Returns the title field variable to use. */
        readonly 'titleField': string;
        /** (read-only) Returns the name of the title font. */
        readonly 'titleFontName': string;
        /** (read-only) Returns the size of the title font. */
        readonly 'titleFontSize': number;
        /** (read-only) Returns the margins of the title. */
        readonly 'titleMargins': Json;
        /** (read-only) Returns the state of the title wrap parameter. */
        readonly 'titleWrap': boolean;
        /** (read-only) Returns the height of the triangle at the bottom of the popup. */
        readonly 'triangleHeight': number;
        /** (read-only) Returns the width of the triangle at the bottom of the popup. */
        readonly 'triangleWidth': number;
        /** (read-only) Returns the vertical offset of the billboard. */
        readonly 'verticalOffset': number;
    };
    'massif::BalloonPopupStyleBuilder': {
        /** Returns the animation style of the billboard. */
        'animationStyle': Handle;
        /** (read-only) Returns the fade animation type. */
        readonly 'animationStyle.fadeAnimationType': 'ANIMATION_TYPE_NONE' | 'ANIMATION_TYPE_STEP' | 'ANIMATION_TYPE_LINEAR' | 'ANIMATION_TYPE_SMOOTHSTEP' | 'ANIMATION_TYPE_SPRING' | number;
        /** (read-only) Returns the phase-in duration of the animation. */
        readonly 'animationStyle.phaseInDuration': number;
        /** (read-only) Returns the phase-out duration of the animation. */
        readonly 'animationStyle.phaseOutDuration': number;
        /** (read-only) Returns the relative speed of the animation. */
        readonly 'animationStyle.relativeSpeed': number;
        /** (read-only) Returns the size-related animation type. */
        readonly 'animationStyle.sizeAnimationType': 'ANIMATION_TYPE_NONE' | 'ANIMATION_TYPE_STEP' | 'ANIMATION_TYPE_LINEAR' | 'ANIMATION_TYPE_SMOOTHSTEP' | 'ANIMATION_TYPE_SPRING' | number;
        /** Returns the horizontal attaching anchor point of the billboard. */
        'attachAnchorPointX': number;
        /** Returns the vertical attaching anchor point of the billboard. */
        'attachAnchorPointY': number;
        /** Returns the margins for popup buttons. */
        'buttonMargins': Json;
        /** Returns the state of the causes overlap flag. */
        'causesOverlap': boolean;
        /** Returns the color of the vector element. */
        'color': number;
        /** Returns the corner radius of the popup. */
        'cornerRadius': number;
        /** Returns the color of the description. */
        'descriptionColor': number;
        /** Returns the description field variable. If not empty, this variable is used to read actual text string from object meta info. */
        'descriptionField': string;
        /** Returns the name of the description font. */
        'descriptionFontName': string;
        /** Returns the size of the description font. */
        'descriptionFontSize': number;
        /** Returns the margins of the description. */
        'descriptionMargins': Json;
        /** Returns the state of the description wrap parameter. */
        'descriptionWrap': boolean;
        /** Returns the state of the allow overlap flag. */
        'hideIfOverlapped': boolean;
        /** Returns the horizontal offset of the billboard. */
        'horizontalOffset': number;
        /** Returns the background color of the left part of the popup. */
        'leftColor': number;
        /** Returns the image of the left part of the popup. */
        'leftImage': Handle;
        /** (read-only) Returns the bytes per pixel parameter of this bitmap. Valid values are 1, 2, 3 and 4. */
        readonly 'leftImage.bytesPerPixel': number;
        /** (read-only) Returns the color format of this bitmap. */
        readonly 'leftImage.colorFormat': 'COLOR_FORMAT_UNSUPPORTED' | 'COLOR_FORMAT_BGRA' | 'COLOR_FORMAT_RGBA_4444' | 'COLOR_FORMAT_RGB_565' | number;
        /** (read-only) Returns the height of the bitmap. */
        readonly 'leftImage.height': number;
        /** (read-only) Returns the width of the bitmap. */
        readonly 'leftImage.width': number;
        /** Returns the margins of the left part of the popup. */
        'leftMargins': Json;
        /** Returns the placement priority of the billboard. */
        'placementPriority': number;
        /** Returns the background color of the right part of the popup. */
        'rightColor': number;
        /** Returns the image of the right part of the popup. */
        'rightImage': Handle;
        /** (read-only) Returns the bytes per pixel parameter of this bitmap. Valid values are 1, 2, 3 and 4. */
        readonly 'rightImage.bytesPerPixel': number;
        /** (read-only) Returns the color format of this bitmap. */
        readonly 'rightImage.colorFormat': 'COLOR_FORMAT_UNSUPPORTED' | 'COLOR_FORMAT_BGRA' | 'COLOR_FORMAT_RGBA_4444' | 'COLOR_FORMAT_RGB_565' | number;
        /** (read-only) Returns the height of the bitmap. */
        readonly 'rightImage.height': number;
        /** (read-only) Returns the width of the bitmap. */
        readonly 'rightImage.width': number;
        /** Returns the margins of the right part of the popup. */
        'rightMargins': Json;
        /** Returns the state of the scale with DPI flag. */
        'scaleWithDPI': boolean;
        /** Returns the color of the stroke surrounding the popup. */
        'strokeColor': number;
        /** Returns the width of the stroke surrounding the popup. */
        'strokeWidth': number;
        /** Returns the color of the title. */
        'titleColor': number;
        /** Returns the title field variable. If not empty, this variable is used to read actual text string from object meta info. */
        'titleField': string;
        /** Returns the name of the title font. */
        'titleFontName': string;
        /** Returns the size of the title font. */
        'titleFontSize': number;
        /** Returns the margins of the title. */
        'titleMargins': Json;
        /** Returns the state of the title wrap parameter. */
        'titleWrap': boolean;
        /** Returns the height of the triangle at the bottom of the popup. */
        'triangleHeight': number;
        /** Returns the width of the triangle at the bottom of the popup. */
        'triangleWidth': number;
        /** Returns the vertical offset of the billboard. */
        'verticalOffset': number;
    };
    'massif::BaseMapView': {
        /** (read-only) Returns the position the camera itself is above (the viewpoint), which at a low tilt is far from the focus it looks at. Where a top-down view has to be centred to come back to the same place. */
        readonly 'cameraPos': Position;
        /** (read-only) Returns true while a flyTo animation is running. */
        readonly 'flightActive': boolean;
        /** (read-only) How far along a flyTo animation is, from 0 to 1, or -1 when none is running. The value the camera is actually at, so an app animating its own state alongside the move reads it rather than its own clock. */
        readonly 'flightProgress': number;
        /** (read-only) Returns the position that the camera is currently looking at. */
        readonly 'focusPos': Position;
        /** (read-only) Returns the MapRenderer object, that can be used for controlling rendering options. */
        readonly 'mapRenderer': Handle;
        /** Returns the map renderer listener. Can be null. */
        'mapRenderer.mapRendererListener': Handle;
        /** Returns the current post-process effect. Can be null. */
        'mapRenderer.postProcessEffect': Handle;
        /** (read-only) Returns the fragment shader source of the effect. */
        readonly 'mapRenderer.postProcessEffect.fragmentShader': string;
        /** (read-only) Returns the name of the effect. */
        readonly 'mapRenderer.postProcessEffect.name': string;
        /** Returns true if the effect needs the terrain depth pre-pass (uTerrainDepthTex). */
        'mapRenderer.postProcessEffect.terrainDepthRequired': boolean;
        /** Returns true if the effect wants the terrain surface normal in the depth pre-pass. */
        'mapRenderer.postProcessEffect.terrainNormalsRequired': boolean;
        /** (read-only) Returns the map rotation in degrees. 0 means looking north, 90 means west, -90 means east and 180 means south. */
        readonly 'rotation': number;
        /** (read-only) Returns the tilt angle in degrees. 0 means looking directly at the horizon, 90 means looking directly down. */
        readonly 'tilt': number;
        /** (read-only) Returns the zoom level. The value returned is never negative, 0 means absolutely zoomed out and all other values describe some level of zoom. */
        readonly 'zoom': number;
    };
    'massif::Billboard': {
        /** Returns the base billboard this billboard is attached to. */
        'baseBillboard': Handle;
        /** (read-only) Returns the bounds of this billboard or the base billboard, if there is one. */
        readonly 'bounds': Bounds;
        /** Returns the geometry object that defines the location of this billboard. */
        'geometry': Handle;
        /** (read-only) Returns the minimal bounds for the geometry. */
        readonly 'geometry.bounds': Bounds;
        readonly 'geometry.centerPos': Position;
        /** (read-only) Returns the geometry as a GeoJSON string, in its own coordinates. Here rather than only on Feature because serialising a shape otherwise means constructing a GeoJSONGeometryWriter, which no string-based binding can do. */
        readonly 'geometry.geoJSON': string;
        readonly 'geometry.type': 'GEOMETRY_TYPE_POINT' | 'GEOMETRY_TYPE_LINE' | 'GEOMETRY_TYPE_POLYGON' | 'GEOMETRY_TYPE_MULTIPOINT' | 'GEOMETRY_TYPE_MULTILINE' | 'GEOMETRY_TYPE_MULTIPOLYGON' | 'GEOMETRY_TYPE_COLLECTION' | number;
        /** Returns the internal id of this vector element. */
        'id': number;
        /** Returns a copy of the vector element meta data map. The changes you make to this map are NOT reflected in the actual meta data of the element. */
        'metaData': Record<string, Json>;
        /** Returns a copy of the vector element meta data map. The changes you make to this map are NOT reflected in the actual meta data of the element. */
        [key: `metaData.${string}`]: Json;
        /** (read-only) Returns the location of the root billboard: getGeometry() if this billboard has a location, otherwise the location found by following the chain of base billboards to its root. */
        readonly 'rootGeometry': Handle;
        /** (read-only) Returns the minimal bounds for the geometry. */
        readonly 'rootGeometry.bounds': Bounds;
        readonly 'rootGeometry.centerPos': Position;
        /** (read-only) Returns the geometry as a GeoJSON string, in its own coordinates. Here rather than only on Feature because serialising a shape otherwise means constructing a GeoJSONGeometryWriter, which no string-based binding can do. */
        readonly 'rootGeometry.geoJSON': string;
        readonly 'rootGeometry.type': 'GEOMETRY_TYPE_POINT' | 'GEOMETRY_TYPE_LINE' | 'GEOMETRY_TYPE_POLYGON' | 'GEOMETRY_TYPE_MULTIPOINT' | 'GEOMETRY_TYPE_MULTILINE' | 'GEOMETRY_TYPE_MULTIPOLYGON' | 'GEOMETRY_TYPE_COLLECTION' | number;
        /** Returns the rotation angle of this billboard. */
        'rotation': number;
        /** Returns the state of the visibility flag of this vector element. */
        'visible': boolean;
    };
    'massif::BillboardStyle': {
        /** (read-only) Returns the animation style of the billboard. */
        readonly 'animationStyle': Handle;
        /** (read-only) Returns the fade animation type. */
        readonly 'animationStyle.fadeAnimationType': 'ANIMATION_TYPE_NONE' | 'ANIMATION_TYPE_STEP' | 'ANIMATION_TYPE_LINEAR' | 'ANIMATION_TYPE_SMOOTHSTEP' | 'ANIMATION_TYPE_SPRING' | number;
        /** (read-only) Returns the phase-in duration of the animation. */
        readonly 'animationStyle.phaseInDuration': number;
        /** (read-only) Returns the phase-out duration of the animation. */
        readonly 'animationStyle.phaseOutDuration': number;
        /** (read-only) Returns the relative speed of the animation. */
        readonly 'animationStyle.relativeSpeed': number;
        /** (read-only) Returns the size-related animation type. */
        readonly 'animationStyle.sizeAnimationType': 'ANIMATION_TYPE_NONE' | 'ANIMATION_TYPE_STEP' | 'ANIMATION_TYPE_LINEAR' | 'ANIMATION_TYPE_SMOOTHSTEP' | 'ANIMATION_TYPE_SPRING' | number;
        /** (read-only) Returns the horizontal attaching anchor point of the billoard. */
        readonly 'attachAnchorPointX': number;
        /** (read-only) Returns the vertical attaching anchor point of the billoard. */
        readonly 'attachAnchorPointY': number;
        /** (read-only) Returns the state of the causes overlap flag. */
        readonly 'causesOverlap': boolean;
        /** (read-only) Returns the color of the vector element. */
        readonly 'color': number;
        /** (read-only) Returns the state of the allow overlap flag. */
        readonly 'hideIfOverlapped': boolean;
        /** (read-only) Returns the horizontal offset of the billboard. */
        readonly 'horizontalOffset': number;
        /** (read-only) Returns the placement priority of the billboard. */
        readonly 'placementPriority': number;
        /** (read-only) Returns the state of the scale with DPI flag. */
        readonly 'scaleWithDPI': boolean;
        /** (read-only) Returns the vertical offset of the billboard. */
        readonly 'verticalOffset': number;
    };
    'massif::BillboardStyleBuilder': {
        /** Returns the animation style of the billboard. */
        'animationStyle': Handle;
        /** (read-only) Returns the fade animation type. */
        readonly 'animationStyle.fadeAnimationType': 'ANIMATION_TYPE_NONE' | 'ANIMATION_TYPE_STEP' | 'ANIMATION_TYPE_LINEAR' | 'ANIMATION_TYPE_SMOOTHSTEP' | 'ANIMATION_TYPE_SPRING' | number;
        /** (read-only) Returns the phase-in duration of the animation. */
        readonly 'animationStyle.phaseInDuration': number;
        /** (read-only) Returns the phase-out duration of the animation. */
        readonly 'animationStyle.phaseOutDuration': number;
        /** (read-only) Returns the relative speed of the animation. */
        readonly 'animationStyle.relativeSpeed': number;
        /** (read-only) Returns the size-related animation type. */
        readonly 'animationStyle.sizeAnimationType': 'ANIMATION_TYPE_NONE' | 'ANIMATION_TYPE_STEP' | 'ANIMATION_TYPE_LINEAR' | 'ANIMATION_TYPE_SMOOTHSTEP' | 'ANIMATION_TYPE_SPRING' | number;
        /** Returns the horizontal attaching anchor point of the billboard. */
        'attachAnchorPointX': number;
        /** Returns the vertical attaching anchor point of the billboard. */
        'attachAnchorPointY': number;
        /** Returns the state of the causes overlap flag. */
        'causesOverlap': boolean;
        /** Returns the color of the vector element. */
        'color': number;
        /** Returns the state of the allow overlap flag. */
        'hideIfOverlapped': boolean;
        /** Returns the horizontal offset of the billboard. */
        'horizontalOffset': number;
        /** Returns the placement priority of the billboard. */
        'placementPriority': number;
        /** Returns the state of the scale with DPI flag. */
        'scaleWithDPI': boolean;
        /** Returns the vertical offset of the billboard. */
        'verticalOffset': number;
    };
    'massif::BinaryData': {
        /** (read-only) Returns the size of the data */
        readonly 'size': number;
    };
    'massif::Bitmap': {
        /** (read-only) Returns the bytes per pixel parameter of this bitmap. Valid values are 1, 2, 3 and 4. */
        readonly 'bytesPerPixel': number;
        /** (read-only) Returns the color format of this bitmap. */
        readonly 'colorFormat': 'COLOR_FORMAT_UNSUPPORTED' | 'COLOR_FORMAT_BGRA' | 'COLOR_FORMAT_RGBA_4444' | 'COLOR_FORMAT_RGB_565' | number;
        /** (read-only) Returns the height of the bitmap. */
        readonly 'height': number;
        /** (read-only) Returns the width of the bitmap. */
        readonly 'width': number;
    };
    'massif::BitmapOverlayRasterTileDataSource': {
        /** (read-only) Returns the extent of the tiles in this data source. The bounds are in coordinate system of the projection of the data source. */
        readonly 'dataExtent': Bounds;
        /** Gets the current maximum overzoom level for this datasource. Over it the datasource will not be "drawn" */
        'maxOverzoomLevel': number;
        /** (read-only) Returns the maximum zoom level supported by this data source. */
        readonly 'maxZoom': number;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        'metaData': Record<string, Json>;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        [key: `metaData.${string}`]: Json;
        /** (read-only) Returns the minimum zoom level supported by this data source. */
        readonly 'minZoom': number;
        /** (read-only) Returns the projection of this tile source. */
        readonly 'projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'projection.bounds': Bounds;
        readonly 'projection.name': string;
    };
    'massif::BundleAssetPackage': {
        readonly 'assetNames': string[];
        /** (read-only) Returns the path of the bundled directory the assets are read from. */
        readonly 'basePath': string;
        /** (read-only) Returns the list of assets found in the bundle, ignoring the base asset package. */
        readonly 'localAssetNames': string[];
    };
    'massif::CacheTileDataSource': {
        'capacity': number;
        /** (read-only) Returns the extent of the tiles in this data source. The bounds are in coordinate system of the projection of the data source. */
        readonly 'dataExtent': Bounds;
        /** (read-only) Returns the original data source that the cache uses. */
        readonly 'dataSource': Handle;
        /** (read-only) Returns the extent of the tiles in this data source. The bounds are in coordinate system of the projection of the data source. */
        readonly 'dataSource.dataExtent': Bounds;
        /** Gets the current maximum overzoom level for this datasource. Over it the datasource will not be "drawn" */
        'dataSource.maxOverzoomLevel': number;
        /** (read-only) Returns the maximum zoom level supported by this data source. */
        readonly 'dataSource.maxZoom': number;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        'dataSource.metaData': Record<string, Json>;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        [key: `dataSource.metaData.${string}`]: Json;
        /** (read-only) Returns the minimum zoom level supported by this data source. */
        readonly 'dataSource.minZoom': number;
        /** (read-only) Returns the projection of this tile source. */
        readonly 'dataSource.projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'dataSource.projection.bounds': Bounds;
        readonly 'dataSource.projection.name': string;
        /** Gets the current maximum overzoom level for this datasource. Over it the datasource will not be "drawn" */
        'maxOverzoomLevel': number;
        /** (read-only) Returns the maximum zoom level supported by this data source. */
        readonly 'maxZoom': number;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        'metaData': Record<string, Json>;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        [key: `metaData.${string}`]: Json;
        /** (read-only) Returns the minimum zoom level supported by this data source. */
        readonly 'minZoom': number;
        /** (read-only) Returns the projection of this tile source. */
        readonly 'projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'projection.bounds': Bounds;
        readonly 'projection.name': string;
    };
    'massif::CartoCSSStyleSet': {
        /** (read-only) Returns the style asset package. */
        readonly 'assetPackage': Handle;
        readonly 'assetPackage.assetNames': string[];
        /** (read-only) Returns the CartoCSS string used for the style. */
        readonly 'cartoCSS': string;
    };
    'massif::CelestialArc': {
        /** (read-only) Returns the altitude of a direction-anchored object. */
        readonly 'altitude': number;
        /** (read-only) Returns the azimuth of a direction-anchored object. */
        readonly 'azimuth': number;
        /** Returns whether the part of the curve below the horizon is drawn. */
        'belowHorizonVisible': boolean;
        /** Returns the click radius of the curve. */
        'clickRadius': number;
        /** Returns the color of the object. */
        'color': number;
        /** (read-only) Returns true if the object is anchored by direction, false if by geographic position. */
        readonly 'directionAnchored': boolean;
        /** (read-only) Returns the distance of a direction-anchored object. */
        readonly 'distance': number;
        /** Returns a copy of the meta data map. Changes to the copy are not reflected in the object. */
        'metaData': Record<string, Json>;
        /** Returns a copy of the meta data map. Changes to the copy are not reflected in the object. */
        [key: `metaData.${string}`]: Json;
        /** Returns whether the map in front hides the object. */
        'occludedByMap': boolean;
        /** (read-only) Returns the geographic position of a position-anchored object. */
        readonly 'position': Position;
        /** (read-only) Returns the altitude of a position-anchored object. */
        readonly 'positionAltitude': number;
        /** (read-only) Returns the angular radius of a circular arc. */
        readonly 'radius': number;
        /** (read-only) Returns true if the directions are read as separate segments rather than as a path. */
        readonly 'segmented': boolean;
        /** Returns the visibility of the object. */
        'visible': boolean;
        /** Returns the line width. */
        'width': number;
    };
    'massif::CelestialClickInfo': {
        /** (read-only) Returns the altitude of the clicked object at the time of the click. */
        readonly 'altitude': number;
        /** (read-only) Returns the azimuth of the clicked object at the time of the click. */
        readonly 'azimuth': number;
        /** (read-only) Returns the clicked object. */
        readonly 'celestialObject': Handle;
        /** (read-only) Returns the altitude of a direction-anchored object. */
        readonly 'celestialObject.altitude': number;
        /** (read-only) Returns the azimuth of a direction-anchored object. */
        readonly 'celestialObject.azimuth': number;
        /** Returns the color of the object. */
        'celestialObject.color': number;
        /** (read-only) Returns true if the object is anchored by direction, false if by geographic position. */
        readonly 'celestialObject.directionAnchored': boolean;
        /** (read-only) Returns the distance of a direction-anchored object. */
        readonly 'celestialObject.distance': number;
        /** Returns a copy of the meta data map. Changes to the copy are not reflected in the object. */
        'celestialObject.metaData': Record<string, Json>;
        /** Returns a copy of the meta data map. Changes to the copy are not reflected in the object. */
        [key: `celestialObject.metaData.${string}`]: Json;
        /** Returns whether the map in front hides the object. */
        'celestialObject.occludedByMap': boolean;
        /** (read-only) Returns the geographic position of a position-anchored object. */
        readonly 'celestialObject.position': Position;
        /** (read-only) Returns the altitude of a position-anchored object. */
        readonly 'celestialObject.positionAltitude': number;
        /** Returns the visibility of the object. */
        'celestialObject.visible': boolean;
        /** (read-only) Returns the click info. */
        readonly 'clickInfo': ClickInfo;
        /** (read-only) Returns the click type. */
        readonly 'clickType': 'CLICK_TYPE_SINGLE' | 'CLICK_TYPE_LONG' | 'CLICK_TYPE_DOUBLE' | 'CLICK_TYPE_DUAL' | number;
    };
    'massif::CelestialEventListener': {
    };
    'massif::CelestialLabel': {
        /** (read-only) Returns the altitude of a direction-anchored object. */
        readonly 'altitude': number;
        /** (read-only) Returns the horizontal anchor point. */
        readonly 'anchorPointX': number;
        /** (read-only) Returns the vertical anchor point. */
        readonly 'anchorPointY': number;
        /** (read-only) Returns the azimuth of a direction-anchored object. */
        readonly 'azimuth': number;
        /** Returns the background colour. */
        'backgroundColor': number;
        /** Returns the corner radius of the plate. */
        'backgroundRadius': number;
        /** Returns whether a click on the label hits it. */
        'clickable': boolean;
        /** Returns the color of the object. */
        'color': number;
        /** (read-only) Returns true if the object is anchored by direction, false if by geographic position. */
        readonly 'directionAnchored': boolean;
        /** (read-only) Returns the distance of a direction-anchored object. */
        readonly 'distance': number;
        /** Returns the font list. */
        'fontName': string;
        /** Returns the font size. */
        'fontSize': number;
        /** Returns the halo colour. */
        'haloColor': number;
        /** Returns the halo width. */
        'haloWidth': number;
        /** Returns a copy of the meta data map. Changes to the copy are not reflected in the object. */
        'metaData': Record<string, Json>;
        /** Returns a copy of the meta data map. Changes to the copy are not reflected in the object. */
        [key: `metaData.${string}`]: Json;
        /** Returns whether the map in front hides the object. */
        'occludedByMap': boolean;
        /** (read-only) Returns the horizontal offset. */
        readonly 'offsetX': number;
        /** (read-only) Returns the vertical offset. */
        readonly 'offsetY': number;
        /** Returns the horizontal padding between the text and the plate's edge. */
        'paddingX': number;
        /** Returns the vertical padding between the text and the plate's edge. */
        'paddingY': number;
        /** (read-only) Returns the geographic position of a position-anchored object. */
        readonly 'position': Position;
        /** (read-only) Returns the altitude of a position-anchored object. */
        readonly 'positionAltitude': number;
        /** Returns the text. */
        'text': string;
        /** Returns the text colour. The object's own colour tints the whole label, plate included. */
        'textColor': number;
        /** Returns the visibility of the object. */
        'visible': boolean;
    };
    'massif::CelestialLayer': {
        /** Returns the object event listener. */
        'celestialEventListener': Handle;
        /** Returns the culling delay of the layer in milliseconds. */
        'cullDelay': number;
        /** Returns a copy of the layer meta data map. The changes you make to this map are NOT reflected in the actual meta data of the layer. */
        'metaData': Record<string, Json>;
        /** Returns a copy of the layer meta data map. The changes you make to this map are NOT reflected in the actual meta data of the layer. */
        [key: `metaData.${string}`]: Json;
        /** Returns the opacity of this layer. */
        'opacity': number;
        /** Returns whether this layer goes through the post-process effect. */
        'postProcessed': boolean;
        /** Returns the layer task priority of this layer. */
        'updatePriority': number;
        /** Returns the visibility of this layer. */
        'visible': boolean;
        /** Returns the visible zoom range of this layer. */
        'visibleZoomRange': [number, number];
    };
    'massif::CelestialObject': {
        /** (read-only) Returns the altitude of a direction-anchored object. */
        readonly 'altitude': number;
        /** (read-only) Returns the azimuth of a direction-anchored object. */
        readonly 'azimuth': number;
        /** Returns the color of the object. */
        'color': number;
        /** (read-only) Returns true if the object is anchored by direction, false if by geographic position. */
        readonly 'directionAnchored': boolean;
        /** (read-only) Returns the distance of a direction-anchored object. */
        readonly 'distance': number;
        /** Returns a copy of the meta data map. Changes to the copy are not reflected in the object. */
        'metaData': Record<string, Json>;
        /** Returns a copy of the meta data map. Changes to the copy are not reflected in the object. */
        [key: `metaData.${string}`]: Json;
        /** Returns whether the map in front hides the object. */
        'occludedByMap': boolean;
        /** (read-only) Returns the geographic position of a position-anchored object. */
        readonly 'position': Position;
        /** (read-only) Returns the altitude of a position-anchored object. */
        readonly 'positionAltitude': number;
        /** Returns the visibility of the object. */
        'visible': boolean;
    };
    'massif::CelestialSprite': {
        /** (read-only) Returns the altitude of a direction-anchored object. */
        readonly 'altitude': number;
        /** Returns the angular size of the sprite. */
        'angularSize': number;
        /** (read-only) Returns the azimuth of a direction-anchored object. */
        readonly 'azimuth': number;
        /** Returns the bitmap of the sprite. */
        'bitmap': Handle;
        /** (read-only) Returns the bytes per pixel parameter of this bitmap. Valid values are 1, 2, 3 and 4. */
        readonly 'bitmap.bytesPerPixel': number;
        /** (read-only) Returns the color format of this bitmap. */
        readonly 'bitmap.colorFormat': 'COLOR_FORMAT_UNSUPPORTED' | 'COLOR_FORMAT_BGRA' | 'COLOR_FORMAT_RGBA_4444' | 'COLOR_FORMAT_RGB_565' | number;
        /** (read-only) Returns the height of the bitmap. */
        readonly 'bitmap.height': number;
        /** (read-only) Returns the width of the bitmap. */
        readonly 'bitmap.width': number;
        /** Returns the extra radius that responds to a click. */
        'clickRadius': number;
        /** Returns the color of the object. */
        'color': number;
        /** (read-only) Returns true if the object is anchored by direction, false if by geographic position. */
        readonly 'directionAnchored': boolean;
        /** (read-only) Returns the distance of a direction-anchored object. */
        readonly 'distance': number;
        /** Returns a copy of the meta data map. Changes to the copy are not reflected in the object. */
        'metaData': Record<string, Json>;
        /** Returns a copy of the meta data map. Changes to the copy are not reflected in the object. */
        [key: `metaData.${string}`]: Json;
        /** Returns whether the map in front hides the object. */
        'occludedByMap': boolean;
        /** (read-only) Returns the geographic position of a position-anchored object. */
        readonly 'position': Position;
        /** (read-only) Returns the altitude of a position-anchored object. */
        readonly 'positionAltitude': number;
        /** Returns the screen size of the sprite. */
        'screenSize': number;
        /** Returns the edge softness of a disc sprite. */
        'softness': number;
        /** Returns the visibility of the object. */
        'visible': boolean;
    };
    'massif::ClickInfo': {
        /** (read-only) Returns the click type. */
        readonly 'clickType': 'CLICK_TYPE_SINGLE' | 'CLICK_TYPE_LONG' | 'CLICK_TYPE_DOUBLE' | 'CLICK_TYPE_DUAL' | number;
        /** (read-only) Returns the click duration in seconds. */
        readonly 'duration': number;
    };
    'massif::ClusterElementBuilder': {
    };
    'massif::ClusterFetchTask': {
    };
    'massif::ClusteredVectorLayer': {
        /** Returns true if Z-buffering is enabled for 2D geometry. By default it is disabled and used only for billboards. */
        'ZBuffering': boolean;
        /** Returns the cluster animation flag value. */
        'animatedClusters': boolean;
        /** (read-only) Returns the current callback used for creating cluster elements. */
        readonly 'clusterElementBuilder': Handle;
        /** Returns the culling delay of the layer in milliseconds. */
        'cullDelay': number;
        /** (read-only) Returns the vector data source of this vector layer. */
        readonly 'dataSource': Handle;
        /** (read-only) Returns the extent of the data of this data source. The bounds are in coordinate system of the projection of the data source. */
        readonly 'dataSource.dataExtent': Bounds;
        /** (read-only) Returns the projection used by this data source. */
        readonly 'dataSource.projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'dataSource.projection.bounds': Bounds;
        readonly 'dataSource.projection.name': string;
        /** Returns the maximum zoom level when clusters are shown. If zoom level is greater, then clusters are replaced with individual elements. Default is 24. */
        'maximumClusterZoom': number;
        /** Returns a copy of the layer meta data map. The changes you make to this map are NOT reflected in the actual meta data of the layer. */
        'metaData': Record<string, Json>;
        /** Returns a copy of the layer meta data map. The changes you make to this map are NOT reflected in the actual meta data of the layer. */
        [key: `metaData.${string}`]: Json;
        /** Returns the current minimum distance between clusters (in device-independent pixels). */
        'minimumClusterDistance': number;
        /** Returns the opacity of this layer. */
        'opacity': number;
        /** Returns whether this layer goes through the post-process effect. */
        'postProcessed': boolean;
        /** Returns the layer task priority of this layer. */
        'updatePriority': number;
        /** Returns the vector element event listener. */
        'vectorElementEventListener': Handle;
        /** Returns the visibility of this layer. */
        'visible': boolean;
        /** Returns the visible zoom range of this layer. */
        'visibleZoomRange': [number, number];
    };
    'massif::Color': {
        /** (read-only) Encodes this map color into 32-bit integer value (ARGB format). */
        readonly 'ARGB': number;
        /** (read-only) Returns the alpha component of this map color. */
        readonly 'a': number;
        /** (read-only) Returns the blue component of this map color. */
        readonly 'b': number;
        /** (read-only) Returns the green component of this map color. */
        readonly 'g': number;
        /** (read-only) Returns the red component of this map color. */
        readonly 'r': number;
    };
    'massif::CombinedTileDataSource': {
        /** (read-only) Returns the extent of the tiles in this data source. The bounds are in coordinate system of the projection of the data source. */
        readonly 'dataExtent': Bounds;
        /** Gets the current maximum overzoom level for this datasource. Over it the datasource will not be "drawn" */
        'maxOverzoomLevel': number;
        /** (read-only) Returns the maximum zoom level supported by this data source. */
        readonly 'maxZoom': number;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        'metaData': Record<string, Json>;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        [key: `metaData.${string}`]: Json;
        /** (read-only) Returns the minimum zoom level supported by this data source. */
        readonly 'minZoom': number;
        /** (read-only) Returns the projection of this tile source. */
        readonly 'projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'projection.bounds': Bounds;
        readonly 'projection.name': string;
    };
    'massif::CompiledStyleSet': {
        /** (read-only) Returns the style asset package. */
        readonly 'assetPackage': Handle;
        readonly 'assetPackage.assetNames': string[];
        /** (read-only) Returns the asset name defining the current style name. */
        readonly 'styleAssetName': string;
        /** (read-only) Returns the current style name. */
        readonly 'styleName': string;
    };
    'massif::CompositeVectorTileLayer': {
        /** Returns the tile data source of the associated UTF grid. By default this is null. */
        'UTFGridDataSource': Handle;
        /** (read-only) Returns the extent of the tiles in this data source. The bounds are in coordinate system of the projection of the data source. */
        readonly 'UTFGridDataSource.dataExtent': Bounds;
        /** Gets the current maximum overzoom level for this datasource. Over it the datasource will not be "drawn" */
        'UTFGridDataSource.maxOverzoomLevel': number;
        /** (read-only) Returns the maximum zoom level supported by this data source. */
        readonly 'UTFGridDataSource.maxZoom': number;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        'UTFGridDataSource.metaData': Record<string, Json>;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        [key: `UTFGridDataSource.metaData.${string}`]: Json;
        /** (read-only) Returns the minimum zoom level supported by this data source. */
        readonly 'UTFGridDataSource.minZoom': number;
        /** (read-only) Returns the projection of this tile source. */
        readonly 'UTFGridDataSource.projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'UTFGridDataSource.projection.bounds': Bounds;
        readonly 'UTFGridDataSource.projection.name': string;
        /** Returns the UTF grid event listener. */
        'UTFGridEventListener': Handle;
        /** Returns the current display order of the buildings. LAST draws over flat labels too: a label that must clear a building is a billboard one, whose pass runs after the buildings. */
        'buildingRenderOrder': 'VECTOR_TILE_RENDER_ORDER_HIDDEN' | 'VECTOR_TILE_RENDER_ORDER_LAYER' | 'VECTOR_TILE_RENDER_ORDER_LAST' | number;
        /** Returns the click handler layer filter. The filter is given as ECMA regular expression that is applied to qualified layer names. */
        'clickHandlerLayerFilter': string;
        /** Returns the click radius of vector tile features. Units are screen density independent pixels (DP or DIP). */
        'clickRadius': number;
        /** Returns the culling delay of the layer in milliseconds. */
        'cullDelay': number;
        /** (read-only) Returns the data source assigned to this layer. */
        readonly 'dataSource': Handle;
        /** (read-only) Returns the extent of the tiles in this data source. The bounds are in coordinate system of the projection of the data source. */
        readonly 'dataSource.dataExtent': Bounds;
        /** Gets the current maximum overzoom level for this datasource. Over it the datasource will not be "drawn" */
        'dataSource.maxOverzoomLevel': number;
        /** (read-only) Returns the maximum zoom level supported by this data source. */
        readonly 'dataSource.maxZoom': number;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        'dataSource.metaData': Record<string, Json>;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        [key: `dataSource.metaData.${string}`]: Json;
        /** (read-only) Returns the minimum zoom level supported by this data source. */
        readonly 'dataSource.minZoom': number;
        /** (read-only) Returns the projection of this tile source. */
        readonly 'dataSource.projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'dataSource.projection.bounds': Bounds;
        readonly 'dataSource.projection.name': string;
        /** Returns the current frame number. */
        'frameNr': number;
        /** Returns the label blending speed, in full fades per second. */
        'labelBlendingSpeed': number;
        /** Returns how much of the perspective divide a label keeps as it recedes from the camera. */
        'labelPerspectiveScaling': number;
        /** Returns the current display order of the labels. */
        'labelRenderOrder': 'VECTOR_TILE_RENDER_ORDER_HIDDEN' | 'VECTOR_TILE_RENDER_ORDER_LAYER' | 'VECTOR_TILE_RENDER_ORDER_LAST' | number;
        /** Returns the current relative layer blending speed. */
        'layerBlendingSpeed': number;
        /** Gets the current maximum overzoom level for this layer. */
        'maxOverzoomLevel': number;
        /** Gets the current maximum underzoom level for this layer. */
        'maxUnderzoomLevel': number;
        /** Returns a copy of the layer meta data map. The changes you make to this map are NOT reflected in the actual meta data of the layer. */
        'metaData': Record<string, Json>;
        /** Returns a copy of the layer meta data map. The changes you make to this map are NOT reflected in the actual meta data of the layer. */
        [key: `metaData.${string}`]: Json;
        /** Returns the opacity of this layer. */
        'opacity': number;
        /** Returns whether this layer goes through the post-process effect. */
        'postProcessed': boolean;
        /** Returns the state of the preloading flag of this layer. */
        'preloading': boolean;
        readonly 'preloadingTileCount': number;
        /** (read-only) Returns the projection this layer's data is in, which is its data source's. */
        readonly 'projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'projection.bounds': Bounds;
        readonly 'projection.name': string;
        /** Returns the renderer layer filter. The filter is given as ECMA regular expression that is applied to qualified layer names. */
        'rendererLayerFilter': string;
        /** (read-only) Returns the data source assigned to this layer. */
        readonly 'source': Handle;
        /** (read-only) Returns the extent of the tiles in this data source. The bounds are in coordinate system of the projection of the data source. */
        readonly 'source.dataExtent': Bounds;
        /** Gets the current maximum overzoom level for this datasource. Over it the datasource will not be "drawn" */
        'source.maxOverzoomLevel': number;
        /** (read-only) Returns the maximum zoom level supported by this data source. */
        readonly 'source.maxZoom': number;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        'source.metaData': Record<string, Json>;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        [key: `source.metaData.${string}`]: Json;
        /** (read-only) Returns the minimum zoom level supported by this data source. */
        readonly 'source.minZoom': number;
        /** (read-only) Returns the projection of this tile source. */
        readonly 'source.projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'source.projection.bounds': Bounds;
        readonly 'source.projection.name': string;
        /** (read-only) Returns the tile decoder assigned to this layer. */
        readonly 'style': Handle;
        readonly 'style.maxZoom': number;
        readonly 'style.minZoom': number;
        /** Returns the state of the synchronized refresh flag. */
        'synchronizedRefresh': boolean;
        /** Returns the tile cache capacity. */
        'tileCacheCapacity': number;
        /** (read-only) Returns the tile decoder assigned to this layer. */
        readonly 'tileDecoder': Handle;
        readonly 'tileDecoder.maxZoom': number;
        readonly 'tileDecoder.minZoom': number;
        /** Returns the tile load listener. */
        'tileLoadListener': Handle;
        /** Returns the current tile substitution policy. */
        'tileSubstitutionPolicy': 'TILE_SUBSTITUTION_POLICY_ALL' | 'TILE_SUBSTITUTION_POLICY_VISIBLE' | 'TILE_SUBSTITUTION_POLICY_NONE' | number;
        /** Returns the layer task priority of this layer. */
        'updatePriority': number;
        /** Returns the vector tile event listener. */
        'vectorTileEventListener': Handle;
        /** Returns the visibility of this layer. */
        'visible': boolean;
        /** (read-only) How many tiles the last cull put on screen. A diagnostic: it is what the tile LOD numbers actually cost. */
        readonly 'visibleTileCount': number;
        /** Returns the visible zoom range of this layer. */
        'visibleZoomRange': [number, number];
        /** Gets the current zoom level bias for this layer. */
        'zoomLevelBias': number;
    };
    'massif::ContourTileDataSource': {
        /** Returns the base contour interval in meters. */
        'baseInterval': number;
        /** (read-only) Returns the extent of the tiles in this data source. The bounds are in coordinate system of the projection of the data source. */
        readonly 'dataExtent': Bounds;
        /** Returns the contour interval used for label stubs. */
        'labelInterval': number;
        /** Returns whether only short label stubs are generated instead of full contour lines. */
        'labelStubsEnabled': boolean;
        /** Returns the name of the generated vector tile layer. */
        'layerName': string;
        /** Gets the current maximum overzoom level for this datasource. Over it the datasource will not be "drawn" */
        'maxOverzoomLevel': number;
        /** (read-only) Returns the maximum zoom level supported by this data source. */
        readonly 'maxZoom': number;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        'metaData': Record<string, Json>;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        [key: `metaData.${string}`]: Json;
        /** Returns the minimum zoom at which contour geometry is generated. */
        'minVisibleZoom': number;
        /** (read-only) Returns the minimum zoom level supported by this data source. */
        readonly 'minZoom': number;
        /** (read-only) Returns the projection of this tile source. */
        readonly 'projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'projection.bounds': Bounds;
        readonly 'projection.name': string;
        /** Returns the target grid resolution used for contour tracing. */
        'resolution': number;
        /** Returns whether seamless tile edges are enabled. */
        'seamlessEdgesEnabled': boolean;
        /** Returns the simplification tolerance in tile pixels. */
        'simplifyTolerance': number;
        /** Returns the terrain options whose elevation manager the label stubs read. */
        'terrainOptions': Handle;
        /** Returns how long the terrain takes to sink flat. */
        'terrainOptions.autoFlattenDuration': number;
        /** Returns the screen parallax below which the terrain renders flat. */
        'terrainOptions.autoFlattenParallax': number;
        /** Returns how long the terrain takes to rise back into 3D. */
        'terrainOptions.autoFlattenRiseDuration': number;
        /** Returns the tilt at or above which the terrain renders flat. */
        'terrainOptions.autoFlattenTilt': number;
        /** Returns the terrain background color. */
        'terrainOptions.backgroundColor': number;
        /** Returns the billboard/label terrain occlusion state. */
        'terrainOptions.billboardOcclusionEnabled': boolean;
        /** Returns the billboard/label terrain occlusion tolerance. */
        'terrainOptions.billboardOcclusionTolerance': number;
        /** Returns whether bridges and tunnels stand on their own chord (3D bridges). */
        'terrainOptions.bridges3DEnabled': boolean;
        /** Returns the duration of the camera terrain-following correction animation. */
        'terrainOptions.cameraClampDuration': number;
        /** Returns the camera terrain clearance floor: an explicit minimum height the camera is kept above the terrain surface, in meters. */
        'terrainOptions.cameraClearance': number;
        /** Returns the share of the camera's altitude that the terrain clearance takes. */
        'terrainOptions.cameraClearanceFraction': number;
        /** Returns the clip-space depth bias used when depth-testing draped 2D geometry against the terrain. */
        'terrainOptions.depthBias': number;
        /** Returns the drape cache budget in megabytes. */
        'terrainOptions.drapeCacheSize': number;
        /** Returns whether polygon fills are draped as a render-to-texture surface. */
        'terrainOptions.drapeFillsEnabled': boolean;
        /** Returns whether vt tile lines are also draped (in addition to fills). */
        'terrainOptions.drapeLinesEnabled': boolean;
        /** Returns the per-tile drape texture resolution, 0 when it follows the screen. */
        'terrainOptions.drapeResolution': number;
        /** Returns how many drape tiles the automatic resolution assumes are cached at once. */
        'terrainOptions.drapeWorkingSet': number;
        /** Returns the elevation grid cache budget in megabytes, 0 for the SDK's own rule. */
        'terrainOptions.elevationCacheSize': number;
        /** Returns whether elevation tile prefetching is enabled. */
        'terrainOptions.elevationPrefetchEnabled': boolean;
        /** Returns the enabled state of the terrain. */
        'terrainOptions.enabled': boolean;
        /** Returns the terrain height exaggeration factor. */
        'terrainOptions.exaggeration': number;
        /** Returns how far a flattened terrain goes back towards a plain 2D map. */
        'terrainOptions.flattenMode': 'TERRAIN_FLATTEN_MODE_RENDER' | 'TERRAIN_FLATTEN_MODE_FULL' | number;
        /** Returns how far the terrain is flattened right now, 0 (full 3D) to 1 (flat). */
        'terrainOptions.flattenRatio': number;
        /** Returns whether the map is asked to render flat. This is the 2D/3D state, whether it was set by the app or by auto-flattening; the switch itself is animated, so for a moment after a change the map is still on its way there. */
        'terrainOptions.flattened': boolean;
        /** Returns the height the viewpoint is lifted above the ground-following focus, in meters. */
        'terrainOptions.focusLift': number;
        /** Returns how many zoom levels below the camera a tile may coarsen to. */
        'terrainOptions.maxTileZoomCoarsening': number;
        /** Returns the maximum visible tile zoom offset, relative to the camera zoom level. */
        'terrainOptions.maxTileZoomOffset': number;
        /** Returns the maximum tile zoom level the terrain mesh is cut at. */
        'terrainOptions.maxZoom': number;
        /** Returns how many terrain surface meshes may be cached. */
        'terrainOptions.meshCacheSize': number;
        /** Returns the terrain mesh resolution. */
        'terrainOptions.meshResolution': number;
        /** Returns the minimum tile zoom level with 3D terrain. */
        'terrainOptions.minZoom': number;
        /** Returns the style layers that are kept out of the terrain drape bake. */
        'terrainOptions.noDrapeLayerFilter': string;
        /** Returns the ground distance the surface normals are measured over, in meters. */
        'terrainOptions.normalSampleDistance': number;
        /** Returns the downscale factor of the packed depth/normal texture post-process effects read. */
        'terrainOptions.postProcessDownscale': number;
        /** Returns whether seamless tile edge handling is enabled. */
        'terrainOptions.seamlessTileEdgesEnabled': boolean;
        /** Returns whether the shared ground pass draws the terrain a second time. */
        'terrainOptions.sharedGroundEnabled': boolean;
        /** Returns the distance geo-three's terrain LOD subdivides at. */
        'terrainOptions.subdivideDistance': number;
        /** Returns the resolution the elevation node field is built at. */
        'terrainOptions.surfaceNodeResolution': number;
        /** Returns the custom terrain surface fragment shader source, or an empty string if no shaded surface is drawn. */
        'terrainOptions.surfaceShaderSource': string;
        /** (read-only) Returns whether the switch is holding the ground flat while the tiles 3D needs load. */
        readonly 'terrainOptions.switching': boolean;
        /** Returns the opacity a label keeps while its anchor is behind 3D content. */
        'terrainOptions.textOcclusionOpacity': number;
        /** Returns whether cross-LOD tile edge stitching is enabled. */
        'terrainOptions.tileEdgeStitchingEnabled': boolean;
        /** Returns the minimum view distance, in meters. */
        'terrainOptions.viewDistance': number;
        /** Returns the factor applied to the view distance. */
        'terrainOptions.viewDistanceFactor': number;
        /** Returns the maximum view distance, in meters. */
        'terrainOptions.viewDistanceMax': number;
    };
    'massif::CullState': {
        /** (read-only) Returns a view state. */
        readonly 'viewState': Json;
    };
    'massif::CustomPopup': {
        /** Returns the horizontal anchor point of this popup. */
        'anchorPointX': number;
        /** Returns the vertical anchor point of this popup. */
        'anchorPointY': number;
        /** Returns the base billboard this billboard is attached to. */
        'baseBillboard': Handle;
        /** Returns the base billboard this billboard is attached to. */
        'baseBillboard.baseBillboard': Handle;
        /** (read-only) Returns the bounds of this billboard or the base billboard, if there is one. */
        readonly 'baseBillboard.bounds': Bounds;
        /** Returns the geometry object that defines the location of this billboard. */
        'baseBillboard.geometry': Handle;
        /** (read-only) Returns the minimal bounds for the geometry. */
        readonly 'baseBillboard.geometry.bounds': Bounds;
        readonly 'baseBillboard.geometry.centerPos': Position;
        /** (read-only) Returns the geometry as a GeoJSON string, in its own coordinates. Here rather than only on Feature because serialising a shape otherwise means constructing a GeoJSONGeometryWriter, which no string-based binding can do. */
        readonly 'baseBillboard.geometry.geoJSON': string;
        readonly 'baseBillboard.geometry.type': 'GEOMETRY_TYPE_POINT' | 'GEOMETRY_TYPE_LINE' | 'GEOMETRY_TYPE_POLYGON' | 'GEOMETRY_TYPE_MULTIPOINT' | 'GEOMETRY_TYPE_MULTILINE' | 'GEOMETRY_TYPE_MULTIPOLYGON' | 'GEOMETRY_TYPE_COLLECTION' | number;
        /** Returns the internal id of this vector element. */
        'baseBillboard.id': number;
        /** Returns a copy of the vector element meta data map. The changes you make to this map are NOT reflected in the actual meta data of the element. */
        'baseBillboard.metaData': Record<string, Json>;
        /** Returns a copy of the vector element meta data map. The changes you make to this map are NOT reflected in the actual meta data of the element. */
        [key: `baseBillboard.metaData.${string}`]: Json;
        /** (read-only) Returns the location of the root billboard: getGeometry() if this billboard has a location, otherwise the location found by following the chain of base billboards to its root. */
        readonly 'baseBillboard.rootGeometry': Handle;
        /** (read-only) Returns the minimal bounds for the geometry. */
        readonly 'baseBillboard.rootGeometry.bounds': Bounds;
        readonly 'baseBillboard.rootGeometry.centerPos': Position;
        /** (read-only) Returns the geometry as a GeoJSON string, in its own coordinates. Here rather than only on Feature because serialising a shape otherwise means constructing a GeoJSONGeometryWriter, which no string-based binding can do. */
        readonly 'baseBillboard.rootGeometry.geoJSON': string;
        readonly 'baseBillboard.rootGeometry.type': 'GEOMETRY_TYPE_POINT' | 'GEOMETRY_TYPE_LINE' | 'GEOMETRY_TYPE_POLYGON' | 'GEOMETRY_TYPE_MULTIPOINT' | 'GEOMETRY_TYPE_MULTILINE' | 'GEOMETRY_TYPE_MULTIPOLYGON' | 'GEOMETRY_TYPE_COLLECTION' | number;
        /** Returns the rotation angle of this billboard. */
        'baseBillboard.rotation': number;
        /** Returns the state of the visibility flag of this vector element. */
        'baseBillboard.visible': boolean;
        /** (read-only) Returns the bounds of this billboard or the base billboard, if there is one. */
        readonly 'bounds': Bounds;
        /** Returns the geometry object that defines the location of this billboard. */
        'geometry': Handle;
        /** (read-only) Returns the minimal bounds for the geometry. */
        readonly 'geometry.bounds': Bounds;
        readonly 'geometry.centerPos': Position;
        /** (read-only) Returns the geometry as a GeoJSON string, in its own coordinates. Here rather than only on Feature because serialising a shape otherwise means constructing a GeoJSONGeometryWriter, which no string-based binding can do. */
        readonly 'geometry.geoJSON': string;
        readonly 'geometry.type': 'GEOMETRY_TYPE_POINT' | 'GEOMETRY_TYPE_LINE' | 'GEOMETRY_TYPE_POLYGON' | 'GEOMETRY_TYPE_MULTIPOINT' | 'GEOMETRY_TYPE_MULTILINE' | 'GEOMETRY_TYPE_MULTIPOLYGON' | 'GEOMETRY_TYPE_COLLECTION' | number;
        /** Returns the internal id of this vector element. */
        'id': number;
        /** Returns a copy of the vector element meta data map. The changes you make to this map are NOT reflected in the actual meta data of the element. */
        'metaData': Record<string, Json>;
        /** Returns a copy of the vector element meta data map. The changes you make to this map are NOT reflected in the actual meta data of the element. */
        [key: `metaData.${string}`]: Json;
        /** (read-only) Returns the handler used for the popup. */
        readonly 'popupHandler': Handle;
        /** (read-only) Returns the location of the root billboard: getGeometry() if this billboard has a location, otherwise the location found by following the chain of base billboards to its root. */
        readonly 'rootGeometry': Handle;
        /** (read-only) Returns the minimal bounds for the geometry. */
        readonly 'rootGeometry.bounds': Bounds;
        readonly 'rootGeometry.centerPos': Position;
        /** (read-only) Returns the geometry as a GeoJSON string, in its own coordinates. Here rather than only on Feature because serialising a shape otherwise means constructing a GeoJSONGeometryWriter, which no string-based binding can do. */
        readonly 'rootGeometry.geoJSON': string;
        readonly 'rootGeometry.type': 'GEOMETRY_TYPE_POINT' | 'GEOMETRY_TYPE_LINE' | 'GEOMETRY_TYPE_POLYGON' | 'GEOMETRY_TYPE_MULTIPOINT' | 'GEOMETRY_TYPE_MULTILINE' | 'GEOMETRY_TYPE_MULTIPOLYGON' | 'GEOMETRY_TYPE_COLLECTION' | number;
        /** Returns the rotation angle of this billboard. */
        'rotation': number;
        /** Returns the style of this Popup. */
        'style': Handle;
        /** (read-only) Returns the animation style of the billboard. */
        readonly 'style.animationStyle': Handle;
        /** (read-only) Returns the fade animation type. */
        readonly 'style.animationStyle.fadeAnimationType': 'ANIMATION_TYPE_NONE' | 'ANIMATION_TYPE_STEP' | 'ANIMATION_TYPE_LINEAR' | 'ANIMATION_TYPE_SMOOTHSTEP' | 'ANIMATION_TYPE_SPRING' | number;
        /** (read-only) Returns the phase-in duration of the animation. */
        readonly 'style.animationStyle.phaseInDuration': number;
        /** (read-only) Returns the phase-out duration of the animation. */
        readonly 'style.animationStyle.phaseOutDuration': number;
        /** (read-only) Returns the relative speed of the animation. */
        readonly 'style.animationStyle.relativeSpeed': number;
        /** (read-only) Returns the size-related animation type. */
        readonly 'style.animationStyle.sizeAnimationType': 'ANIMATION_TYPE_NONE' | 'ANIMATION_TYPE_STEP' | 'ANIMATION_TYPE_LINEAR' | 'ANIMATION_TYPE_SMOOTHSTEP' | 'ANIMATION_TYPE_SPRING' | number;
        /** (read-only) Returns the horizontal attaching anchor point of the billoard. */
        readonly 'style.attachAnchorPointX': number;
        /** (read-only) Returns the vertical attaching anchor point of the billoard. */
        readonly 'style.attachAnchorPointY': number;
        /** (read-only) Returns the state of the causes overlap flag. */
        readonly 'style.causesOverlap': boolean;
        /** (read-only) Returns the color of the vector element. */
        readonly 'style.color': number;
        /** (read-only) Returns the state of the allow overlap flag. */
        readonly 'style.hideIfOverlapped': boolean;
        /** (read-only) Returns the horizontal offset of the billboard. */
        readonly 'style.horizontalOffset': number;
        /** (read-only) Returns the placement priority of the billboard. */
        readonly 'style.placementPriority': number;
        /** (read-only) Returns the state of the scale with DPI flag. */
        readonly 'style.scaleWithDPI': boolean;
        /** (read-only) Returns the vertical offset of the billboard. */
        readonly 'style.verticalOffset': number;
        /** Returns the state of the visibility flag of this vector element. */
        'visible': boolean;
    };
    'massif::CustomPopupHandler': {
    };
    'massif::CustomRasterTileLayer': {
        /** Returns the tile data source of the associated UTF grid. By default this is null. */
        'UTFGridDataSource': Handle;
        /** (read-only) Returns the extent of the tiles in this data source. The bounds are in coordinate system of the projection of the data source. */
        readonly 'UTFGridDataSource.dataExtent': Bounds;
        /** Gets the current maximum overzoom level for this datasource. Over it the datasource will not be "drawn" */
        'UTFGridDataSource.maxOverzoomLevel': number;
        /** (read-only) Returns the maximum zoom level supported by this data source. */
        readonly 'UTFGridDataSource.maxZoom': number;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        'UTFGridDataSource.metaData': Record<string, Json>;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        [key: `UTFGridDataSource.metaData.${string}`]: Json;
        /** (read-only) Returns the minimum zoom level supported by this data source. */
        readonly 'UTFGridDataSource.minZoom': number;
        /** (read-only) Returns the projection of this tile source. */
        readonly 'UTFGridDataSource.projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'UTFGridDataSource.projection.bounds': Bounds;
        readonly 'UTFGridDataSource.projection.name': string;
        /** Returns the UTF grid event listener. */
        'UTFGridEventListener': Handle;
        /** Returns the culling delay of the layer in milliseconds. */
        'cullDelay': number;
        /** (read-only) Returns the data source assigned to this layer. */
        readonly 'dataSource': Handle;
        /** (read-only) Returns the extent of the tiles in this data source. The bounds are in coordinate system of the projection of the data source. */
        readonly 'dataSource.dataExtent': Bounds;
        /** Gets the current maximum overzoom level for this datasource. Over it the datasource will not be "drawn" */
        'dataSource.maxOverzoomLevel': number;
        /** (read-only) Returns the maximum zoom level supported by this data source. */
        readonly 'dataSource.maxZoom': number;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        'dataSource.metaData': Record<string, Json>;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        [key: `dataSource.metaData.${string}`]: Json;
        /** (read-only) Returns the minimum zoom level supported by this data source. */
        readonly 'dataSource.minZoom': number;
        /** (read-only) Returns the projection of this tile source. */
        readonly 'dataSource.projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'dataSource.projection.bounds': Bounds;
        readonly 'dataSource.projection.name': string;
        /** Returns the current frame number. */
        'frameNr': number;
        /** Gets the current maximum overzoom level for this layer. */
        'maxOverzoomLevel': number;
        /** Gets the current maximum underzoom level for this layer. */
        'maxUnderzoomLevel': number;
        /** Returns a copy of the layer meta data map. The changes you make to this map are NOT reflected in the actual meta data of the layer. */
        'metaData': Record<string, Json>;
        /** Returns a copy of the layer meta data map. The changes you make to this map are NOT reflected in the actual meta data of the layer. */
        [key: `metaData.${string}`]: Json;
        /** Returns the opacity of this layer. */
        'opacity': number;
        /** Returns whether this layer goes through the post-process effect. */
        'postProcessed': boolean;
        /** Returns the state of the preloading flag of this layer. */
        'preloading': boolean;
        readonly 'preloadingTileCount': number;
        /** (read-only) Returns the projection this layer's data is in, which is its data source's. */
        readonly 'projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'projection.bounds': Bounds;
        readonly 'projection.name': string;
        /** Returns the raster tile event listener. */
        'rasterTileEventListener': Handle;
        /** Returns the custom fragment shader source. */
        'shaderSource': string;
        /** (read-only) Returns the data source assigned to this layer. */
        readonly 'source': Handle;
        /** (read-only) Returns the extent of the tiles in this data source. The bounds are in coordinate system of the projection of the data source. */
        readonly 'source.dataExtent': Bounds;
        /** Gets the current maximum overzoom level for this datasource. Over it the datasource will not be "drawn" */
        'source.maxOverzoomLevel': number;
        /** (read-only) Returns the maximum zoom level supported by this data source. */
        readonly 'source.maxZoom': number;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        'source.metaData': Record<string, Json>;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        [key: `source.metaData.${string}`]: Json;
        /** (read-only) Returns the minimum zoom level supported by this data source. */
        readonly 'source.minZoom': number;
        /** (read-only) Returns the projection of this tile source. */
        readonly 'source.projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'source.projection.bounds': Bounds;
        readonly 'source.projection.name': string;
        /** Returns the state of the synchronized refresh flag. */
        'synchronizedRefresh': boolean;
        /** Returns the tile texture cache capacity. */
        'textureCacheCapacity': number;
        /** Returns the current relative tile blending speed. */
        'tileBlendingSpeed': number;
        /** Returns the current tile filter mode. */
        'tileFilterMode': 'RASTER_TILE_FILTER_MODE_NEAREST' | 'RASTER_TILE_FILTER_MODE_BILINEAR' | 'RASTER_TILE_FILTER_MODE_BICUBIC' | number;
        /** Returns the tile load listener. */
        'tileLoadListener': Handle;
        /** Returns the current tile substitution policy. */
        'tileSubstitutionPolicy': 'TILE_SUBSTITUTION_POLICY_ALL' | 'TILE_SUBSTITUTION_POLICY_VISIBLE' | 'TILE_SUBSTITUTION_POLICY_NONE' | number;
        /** Returns the layer task priority of this layer. */
        'updatePriority': number;
        /** Returns the visibility of this layer. */
        'visible': boolean;
        /** (read-only) How many tiles the last cull put on screen. A diagnostic: it is what the tile LOD numbers actually cost. */
        readonly 'visibleTileCount': number;
        /** Returns the visible zoom range of this layer. */
        'visibleZoomRange': [number, number];
        /** Gets the current zoom level bias for this layer. */
        'zoomLevelBias': number;
    };
    'massif::DataSourceListener': {
    };
    'massif::DirAssetPackage': {
        readonly 'assetNames': string[];
        /** (read-only) Returns the full path of the directory containing the assets. */
        readonly 'dirPath': string;
        /** (read-only) Returns the list of assets stored in the directory, ignoring the base asset package. */
        readonly 'localAssetNames': string[];
    };
    'massif::DouglasPeuckerGeometrySimplifier': {
    };
    'massif::DownloadTask': {
    };
    'massif::EPSG3857': {
        /** (read-only) Returns the bounds of this projection. */
        readonly 'bounds': Bounds;
        readonly 'name': string;
    };
    'massif::EPSG4326': {
        /** (read-only) Returns the bounds of this projection. */
        readonly 'bounds': Bounds;
        readonly 'name': string;
    };
    'massif::EditableVectorLayer': {
        /** Returns true if Z-buffering is enabled for 2D geometry. By default it is disabled and used only for billboards. */
        'ZBuffering': boolean;
        /** Returns the culling delay of the layer in milliseconds. */
        'cullDelay': number;
        /** (read-only) Returns the vector data source of this vector layer. */
        readonly 'dataSource': Handle;
        /** (read-only) Returns the extent of the data of this data source. The bounds are in coordinate system of the projection of the data source. */
        readonly 'dataSource.dataExtent': Bounds;
        /** (read-only) Returns the projection used by this data source. */
        readonly 'dataSource.projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'dataSource.projection.bounds': Bounds;
        readonly 'dataSource.projection.name': string;
        /** Returns a copy of the layer meta data map. The changes you make to this map are NOT reflected in the actual meta data of the layer. */
        'metaData': Record<string, Json>;
        /** Returns a copy of the layer meta data map. The changes you make to this map are NOT reflected in the actual meta data of the layer. */
        [key: `metaData.${string}`]: Json;
        /** Returns the opacity of this layer. */
        'opacity': number;
        /** Returns whether this layer goes through the post-process effect. */
        'postProcessed': boolean;
        /** Returns the selected vector element. If no element is currently selected, null is returned. */
        'selectedVectorElement': Handle;
        /** (read-only) Returns the bounds of this vector element. */
        readonly 'selectedVectorElement.bounds': Bounds;
        /** (read-only) Returns the geometry object that defines the location of this vector element. */
        readonly 'selectedVectorElement.geometry': Handle;
        /** (read-only) Returns the minimal bounds for the geometry. */
        readonly 'selectedVectorElement.geometry.bounds': Bounds;
        readonly 'selectedVectorElement.geometry.centerPos': Position;
        /** (read-only) Returns the geometry as a GeoJSON string, in its own coordinates. Here rather than only on Feature because serialising a shape otherwise means constructing a GeoJSONGeometryWriter, which no string-based binding can do. */
        readonly 'selectedVectorElement.geometry.geoJSON': string;
        readonly 'selectedVectorElement.geometry.type': 'GEOMETRY_TYPE_POINT' | 'GEOMETRY_TYPE_LINE' | 'GEOMETRY_TYPE_POLYGON' | 'GEOMETRY_TYPE_MULTIPOINT' | 'GEOMETRY_TYPE_MULTILINE' | 'GEOMETRY_TYPE_MULTIPOLYGON' | 'GEOMETRY_TYPE_COLLECTION' | number;
        /** Returns the internal id of this vector element. */
        'selectedVectorElement.id': number;
        /** Returns a copy of the vector element meta data map. The changes you make to this map are NOT reflected in the actual meta data of the element. */
        'selectedVectorElement.metaData': Record<string, Json>;
        /** Returns a copy of the vector element meta data map. The changes you make to this map are NOT reflected in the actual meta data of the element. */
        [key: `selectedVectorElement.metaData.${string}`]: Json;
        /** Returns the state of the visibility flag of this vector element. */
        'selectedVectorElement.visible': boolean;
        /** Returns the layer task priority of this layer. */
        'updatePriority': number;
        /** Returns the current edit event listener for the layer. */
        'vectorEditEventListener': Handle;
        /** Returns the vector element event listener. */
        'vectorElementEventListener': Handle;
        /** Returns the visibility of this layer. */
        'visible': boolean;
        /** Returns the visible zoom range of this layer. */
        'visibleZoomRange': [number, number];
    };
    'massif::ElevationDecoder': {
    };
    'massif::EventListener': {
    };
    'massif::Feature': {
        /** (read-only) Returns the geometry of the feature. */
        readonly 'geometry': Handle;
        /** (read-only) Returns the minimal bounds for the geometry. */
        readonly 'geometry.bounds': Bounds;
        readonly 'geometry.centerPos': Position;
        /** (read-only) Returns the geometry as a GeoJSON string, in its own coordinates. Here rather than only on Feature because serialising a shape otherwise means constructing a GeoJSONGeometryWriter, which no string-based binding can do. */
        readonly 'geometry.geoJSON': string;
        readonly 'geometry.type': 'GEOMETRY_TYPE_POINT' | 'GEOMETRY_TYPE_LINE' | 'GEOMETRY_TYPE_POLYGON' | 'GEOMETRY_TYPE_MULTIPOINT' | 'GEOMETRY_TYPE_MULTILINE' | 'GEOMETRY_TYPE_MULTIPOLYGON' | 'GEOMETRY_TYPE_COLLECTION' | number;
        /** (read-only) Returns the feature's geometry as a GeoJSON string, in its own coordinates. Serialising a geometry otherwise means constructing a GeoJSONGeometryWriter in the binding, which every binding then does differently and, in a scripting one, slowly. */
        readonly 'geometryGeoJSON': string;
        /** (read-only) Returns the properties of the feature. */
        readonly 'properties': Json;
    };
    'massif::FeatureBuilder': {
        /** Returns the geometry of the builder. */
        'geometry': Handle;
        /** (read-only) Returns the minimal bounds for the geometry. */
        readonly 'geometry.bounds': Bounds;
        readonly 'geometry.centerPos': Position;
        /** (read-only) Returns the geometry as a GeoJSON string, in its own coordinates. Here rather than only on Feature because serialising a shape otherwise means constructing a GeoJSONGeometryWriter, which no string-based binding can do. */
        readonly 'geometry.geoJSON': string;
        readonly 'geometry.type': 'GEOMETRY_TYPE_POINT' | 'GEOMETRY_TYPE_LINE' | 'GEOMETRY_TYPE_POLYGON' | 'GEOMETRY_TYPE_MULTIPOINT' | 'GEOMETRY_TYPE_MULTILINE' | 'GEOMETRY_TYPE_MULTIPOLYGON' | 'GEOMETRY_TYPE_COLLECTION' | number;
    };
    'massif::FeatureCollection': {
        /** (read-only) Returns the number of features in this container. */
        readonly 'featureCount': number;
    };
    'massif::FeatureCollectionSearchService': {
        /** (read-only) Returns the feature collection of the search service. */
        readonly 'featureCollection': Handle;
        /** (read-only) Returns the number of features in this container. */
        readonly 'featureCollection.featureCount': number;
        /** Returns the maximum number of results the search service returns. */
        'maxResults': number;
        /** (read-only) Returns the projection of the feature collection of the search service. */
        readonly 'projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'projection.bounds': Bounds;
        readonly 'projection.name': string;
    };
    'massif::FetchTask': {
    };
    'massif::FetchTaskBase': {
    };
    'massif::FetchingTasks': {
    };
    'massif::FetchingTileTasks': {
    };
    'massif::FogOptions': {
        /** Returns the fog color. */
        'color': number;
        /** Returns whether the fog is drawn at all. */
        'enabled': boolean;
        /** Returns the color of the upper atmosphere. */
        'highColor': number;
        /** Returns how far up the sky the fog is blended in. */
        'horizonBlend': number;
        /** Returns where the fog reaches full strength. */
        'rangeEnd': number;
        /** Returns where the fog starts. */
        'rangeStart': number;
        /** Returns the custom fog fragment shader source, or an empty string if the built-in blend is used. */
        'shaderSource': string;
        /** Returns the color of the sky at the zenith, beyond the atmosphere. */
        'spaceColor': number;
        /** Returns how brightly stars are drawn beyond the atmosphere. */
        'starIntensity': number;
        /** Returns the altitude the fog has fully faded out at. */
        'verticalRangeEnd': number;
        /** Returns the altitude the fog starts fading out at. */
        'verticalRangeStart': number;
    };
    'massif::GeoJSONGeometryReader': {
        /** Returns the current target projection. If target projection is set, all geometry coordinates will be converted from WGS84 to target projection coordinate system. */
        'targetProjection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'targetProjection.bounds': Bounds;
        readonly 'targetProjection.name': string;
    };
    'massif::GeoJSONGeometryWriter': {
        /** Returns the current source projection. If source projection is set, all geometry coordinates will be converted from given coordinate system to WGS84. */
        'sourceProjection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'sourceProjection.bounds': Bounds;
        readonly 'sourceProjection.name': string;
        /** Returns the state of Z coordinate serialization. */
        'z': boolean;
    };
    'massif::GeoJSONVectorTileDataSource': {
        /** (read-only) Returns the extent of the tiles in this data source. The bounds are in coordinate system of the projection of the data source. */
        readonly 'dataExtent': Bounds;
        /** Returns the default layer buffer in tile pixels. */
        'defaultLayerBuffer': number;
        /** Gets the current maximum overzoom level for this datasource. Over it the datasource will not be "drawn" */
        'maxOverzoomLevel': number;
        /** (read-only) Returns the maximum zoom level supported by this data source. */
        readonly 'maxZoom': number;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        'metaData': Record<string, Json>;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        [key: `metaData.${string}`]: Json;
        /** (read-only) Returns the minimum zoom level supported by this data source. */
        readonly 'minZoom': number;
        /** (read-only) Returns the projection of this tile source. */
        readonly 'projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'projection.bounds': Bounds;
        readonly 'projection.name': string;
        /** Returns the simplification tolerance in tile pixels. */
        'simplifyTolerance': number;
    };
    'massif::GeocodingAddress': {
        /** (read-only) Returns the list of category tags describing the address. */
        readonly 'categories': string[];
        /** (read-only) Returns the country name included in the address. */
        readonly 'country': string;
        /** (read-only) Returns the county name included in the address. */
        readonly 'county': string;
        /** (read-only) Returns the house number included in the address. */
        readonly 'houseNumber': string;
        /** (read-only) Returns the locality (city, town, village) name included in the address. */
        readonly 'locality': string;
        /** (read-only) Returns the name included in the address. */
        readonly 'name': string;
        /** (read-only) Returns the local neighbourhood name included in the address. */
        readonly 'neighbourhood': string;
        /** (read-only) Returns the postcode of the address. */
        readonly 'postcode': string;
        /** (read-only) Returns the region name included in the address. */
        readonly 'region': string;
        /** (read-only) Returns the street name included in the address. */
        readonly 'street': string;
    };
    'massif::GeocodingRequest': {
        /** Returns the location attribute of the request. The matching address near the specified location (up to a specified radius) are preferred. */
        'location': Position;
        /** Returns the location radius attribute of the request (in meters). */
        'locationRadius': number;
        /** (read-only) Returns the projection of the request. */
        readonly 'projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'projection.bounds': Bounds;
        readonly 'projection.name': string;
        /** (read-only) Returns the text-based query of the request. */
        readonly 'query': string;
    };
    'massif::GeocodingResult': {
        /** (read-only) Returns the address of the result. */
        readonly 'address': Json;
        /** (read-only) Returns the feature collection of the result. */
        readonly 'featureCollection': Handle;
        /** (read-only) Returns the number of features in this container. */
        readonly 'featureCollection.featureCount': number;
        /** (read-only) Returns the projection of the geometry in the result. */
        readonly 'projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'projection.bounds': Bounds;
        readonly 'projection.name': string;
        /** (read-only) Returns the rank of the result. The rank is a normalized number between 0 and 1, 1 meaning a perfect match. */
        readonly 'rank': number;
    };
    'massif::GeocodingService': {
        'autocomplete': boolean;
        'language': string;
        'maxResults': number;
    };
    'massif::Geometry': {
        /** (read-only) Returns the minimal bounds for the geometry. */
        readonly 'bounds': Bounds;
        readonly 'centerPos': Position;
        /** (read-only) Returns the geometry as a GeoJSON string, in its own coordinates. Here rather than only on Feature because serialising a shape otherwise means constructing a GeoJSONGeometryWriter, which no string-based binding can do. */
        readonly 'geoJSON': string;
        readonly 'type': 'GEOMETRY_TYPE_POINT' | 'GEOMETRY_TYPE_LINE' | 'GEOMETRY_TYPE_POLYGON' | 'GEOMETRY_TYPE_MULTIPOINT' | 'GEOMETRY_TYPE_MULTILINE' | 'GEOMETRY_TYPE_MULTIPOLYGON' | 'GEOMETRY_TYPE_COLLECTION' | number;
    };
    'massif::GeometryCollection': {
        /** (read-only) Returns the bounds of this vector element. */
        readonly 'bounds': Bounds;
        'geometry': Handle;
        /** (read-only) Returns the minimal bounds for the geometry. */
        readonly 'geometry.bounds': Bounds;
        readonly 'geometry.centerPos': Position;
        /** (read-only) Returns the geometry as a GeoJSON string, in its own coordinates. Here rather than only on Feature because serialising a shape otherwise means constructing a GeoJSONGeometryWriter, which no string-based binding can do. */
        readonly 'geometry.geoJSON': string;
        /** (read-only) Returns the number of geometry objects in this multi geometry container. */
        readonly 'geometry.geometryCount': number;
        readonly 'geometry.type': 'GEOMETRY_TYPE_POINT' | 'GEOMETRY_TYPE_LINE' | 'GEOMETRY_TYPE_POLYGON' | 'GEOMETRY_TYPE_MULTIPOINT' | 'GEOMETRY_TYPE_MULTILINE' | 'GEOMETRY_TYPE_MULTIPOLYGON' | 'GEOMETRY_TYPE_COLLECTION' | number;
        /** Returns the internal id of this vector element. */
        'id': number;
        /** Returns a copy of the vector element meta data map. The changes you make to this map are NOT reflected in the actual meta data of the element. */
        'metaData': Record<string, Json>;
        /** Returns a copy of the vector element meta data map. The changes you make to this map are NOT reflected in the actual meta data of the element. */
        [key: `metaData.${string}`]: Json;
        /** Returns the style of this geometry collection. */
        'style': Handle;
        /** (read-only) Returns the color of the vector element. */
        readonly 'style.color': number;
        /** (read-only) Returns the line style. */
        readonly 'style.lineStyle': Handle;
        /** (read-only) Returns the bitmap of the line. */
        readonly 'style.lineStyle.bitmap': Handle;
        /** (read-only) Returns the width of the line used for click detection. */
        readonly 'style.lineStyle.clickWidth': number;
        /** (read-only) Returns the color of the vector element. */
        readonly 'style.lineStyle.color': number;
        /** (read-only) Returns the end point type of the line. */
        readonly 'style.lineStyle.lineEndType': 'LINE_END_TYPE_NONE' | 'LINE_END_TYPE_SQUARE' | 'LINE_END_TYPE_ROUND' | number;
        /** (read-only) Returns the join type of the line. */
        readonly 'style.lineStyle.lineJoinType': 'LINE_JOIN_TYPE_NONE' | 'LINE_JOIN_TYPE_MITER' | 'LINE_JOIN_TYPE_BEVEL' | 'LINE_JOIN_TYPE_ROUND' | number;
        /** (read-only) Returns the stretching factor of the line. */
        readonly 'style.lineStyle.stretchFactor': number;
        /** (read-only) Returns the width of the line. */
        readonly 'style.lineStyle.width': number;
        /** (read-only) Returns the point style. */
        readonly 'style.pointStyle': Handle;
        /** (read-only) Returns the bitmap of the point. */
        readonly 'style.pointStyle.bitmap': Handle;
        /** (read-only) Returns the size of the point used for click detection. */
        readonly 'style.pointStyle.clickSize': number;
        /** (read-only) Returns the color of the vector element. */
        readonly 'style.pointStyle.color': number;
        /** (read-only) Returns the size of the point. */
        readonly 'style.pointStyle.size': number;
        /** (read-only) Returns the polygon style. */
        readonly 'style.polygonStyle': Handle;
        /** (read-only) Returns the color of the vector element. */
        readonly 'style.polygonStyle.color': number;
        /** (read-only) Returns the style of the edges of the polygon. */
        readonly 'style.polygonStyle.lineStyle': Handle;
        /** Returns the state of the visibility flag of this vector element. */
        'visible': boolean;
    };
    'massif::GeometryCollectionStyle': {
        /** (read-only) Returns the color of the vector element. */
        readonly 'color': number;
        /** (read-only) Returns the line style. */
        readonly 'lineStyle': Handle;
        /** (read-only) Returns the bitmap of the line. */
        readonly 'lineStyle.bitmap': Handle;
        /** (read-only) Returns the bytes per pixel parameter of this bitmap. Valid values are 1, 2, 3 and 4. */
        readonly 'lineStyle.bitmap.bytesPerPixel': number;
        /** (read-only) Returns the color format of this bitmap. */
        readonly 'lineStyle.bitmap.colorFormat': 'COLOR_FORMAT_UNSUPPORTED' | 'COLOR_FORMAT_BGRA' | 'COLOR_FORMAT_RGBA_4444' | 'COLOR_FORMAT_RGB_565' | number;
        /** (read-only) Returns the height of the bitmap. */
        readonly 'lineStyle.bitmap.height': number;
        /** (read-only) Returns the width of the bitmap. */
        readonly 'lineStyle.bitmap.width': number;
        /** (read-only) Returns the width of the line used for click detection. */
        readonly 'lineStyle.clickWidth': number;
        /** (read-only) Returns the color of the vector element. */
        readonly 'lineStyle.color': number;
        /** (read-only) Returns the end point type of the line. */
        readonly 'lineStyle.lineEndType': 'LINE_END_TYPE_NONE' | 'LINE_END_TYPE_SQUARE' | 'LINE_END_TYPE_ROUND' | number;
        /** (read-only) Returns the join type of the line. */
        readonly 'lineStyle.lineJoinType': 'LINE_JOIN_TYPE_NONE' | 'LINE_JOIN_TYPE_MITER' | 'LINE_JOIN_TYPE_BEVEL' | 'LINE_JOIN_TYPE_ROUND' | number;
        /** (read-only) Returns the stretching factor of the line. */
        readonly 'lineStyle.stretchFactor': number;
        /** (read-only) Returns the width of the line. */
        readonly 'lineStyle.width': number;
        /** (read-only) Returns the point style. */
        readonly 'pointStyle': Handle;
        /** (read-only) Returns the bitmap of the point. */
        readonly 'pointStyle.bitmap': Handle;
        /** (read-only) Returns the bytes per pixel parameter of this bitmap. Valid values are 1, 2, 3 and 4. */
        readonly 'pointStyle.bitmap.bytesPerPixel': number;
        /** (read-only) Returns the color format of this bitmap. */
        readonly 'pointStyle.bitmap.colorFormat': 'COLOR_FORMAT_UNSUPPORTED' | 'COLOR_FORMAT_BGRA' | 'COLOR_FORMAT_RGBA_4444' | 'COLOR_FORMAT_RGB_565' | number;
        /** (read-only) Returns the height of the bitmap. */
        readonly 'pointStyle.bitmap.height': number;
        /** (read-only) Returns the width of the bitmap. */
        readonly 'pointStyle.bitmap.width': number;
        /** (read-only) Returns the size of the point used for click detection. */
        readonly 'pointStyle.clickSize': number;
        /** (read-only) Returns the color of the vector element. */
        readonly 'pointStyle.color': number;
        /** (read-only) Returns the size of the point. */
        readonly 'pointStyle.size': number;
        /** (read-only) Returns the polygon style. */
        readonly 'polygonStyle': Handle;
        /** (read-only) Returns the color of the vector element. */
        readonly 'polygonStyle.color': number;
        /** (read-only) Returns the style of the edges of the polygon. */
        readonly 'polygonStyle.lineStyle': Handle;
        /** (read-only) Returns the bitmap of the line. */
        readonly 'polygonStyle.lineStyle.bitmap': Handle;
        /** (read-only) Returns the width of the line used for click detection. */
        readonly 'polygonStyle.lineStyle.clickWidth': number;
        /** (read-only) Returns the color of the vector element. */
        readonly 'polygonStyle.lineStyle.color': number;
        /** (read-only) Returns the end point type of the line. */
        readonly 'polygonStyle.lineStyle.lineEndType': 'LINE_END_TYPE_NONE' | 'LINE_END_TYPE_SQUARE' | 'LINE_END_TYPE_ROUND' | number;
        /** (read-only) Returns the join type of the line. */
        readonly 'polygonStyle.lineStyle.lineJoinType': 'LINE_JOIN_TYPE_NONE' | 'LINE_JOIN_TYPE_MITER' | 'LINE_JOIN_TYPE_BEVEL' | 'LINE_JOIN_TYPE_ROUND' | number;
        /** (read-only) Returns the stretching factor of the line. */
        readonly 'polygonStyle.lineStyle.stretchFactor': number;
        /** (read-only) Returns the width of the line. */
        readonly 'polygonStyle.lineStyle.width': number;
    };
    'massif::GeometryCollectionStyleBuilder': {
        /** Returns the color of the vector element. */
        'color': number;
        /** Returns the line style. */
        'lineStyle': Handle;
        /** (read-only) Returns the bitmap of the line. */
        readonly 'lineStyle.bitmap': Handle;
        /** (read-only) Returns the bytes per pixel parameter of this bitmap. Valid values are 1, 2, 3 and 4. */
        readonly 'lineStyle.bitmap.bytesPerPixel': number;
        /** (read-only) Returns the color format of this bitmap. */
        readonly 'lineStyle.bitmap.colorFormat': 'COLOR_FORMAT_UNSUPPORTED' | 'COLOR_FORMAT_BGRA' | 'COLOR_FORMAT_RGBA_4444' | 'COLOR_FORMAT_RGB_565' | number;
        /** (read-only) Returns the height of the bitmap. */
        readonly 'lineStyle.bitmap.height': number;
        /** (read-only) Returns the width of the bitmap. */
        readonly 'lineStyle.bitmap.width': number;
        /** (read-only) Returns the width of the line used for click detection. */
        readonly 'lineStyle.clickWidth': number;
        /** (read-only) Returns the color of the vector element. */
        readonly 'lineStyle.color': number;
        /** (read-only) Returns the end point type of the line. */
        readonly 'lineStyle.lineEndType': 'LINE_END_TYPE_NONE' | 'LINE_END_TYPE_SQUARE' | 'LINE_END_TYPE_ROUND' | number;
        /** (read-only) Returns the join type of the line. */
        readonly 'lineStyle.lineJoinType': 'LINE_JOIN_TYPE_NONE' | 'LINE_JOIN_TYPE_MITER' | 'LINE_JOIN_TYPE_BEVEL' | 'LINE_JOIN_TYPE_ROUND' | number;
        /** (read-only) Returns the stretching factor of the line. */
        readonly 'lineStyle.stretchFactor': number;
        /** (read-only) Returns the width of the line. */
        readonly 'lineStyle.width': number;
        /** Returns the point style. */
        'pointStyle': Handle;
        /** (read-only) Returns the bitmap of the point. */
        readonly 'pointStyle.bitmap': Handle;
        /** (read-only) Returns the bytes per pixel parameter of this bitmap. Valid values are 1, 2, 3 and 4. */
        readonly 'pointStyle.bitmap.bytesPerPixel': number;
        /** (read-only) Returns the color format of this bitmap. */
        readonly 'pointStyle.bitmap.colorFormat': 'COLOR_FORMAT_UNSUPPORTED' | 'COLOR_FORMAT_BGRA' | 'COLOR_FORMAT_RGBA_4444' | 'COLOR_FORMAT_RGB_565' | number;
        /** (read-only) Returns the height of the bitmap. */
        readonly 'pointStyle.bitmap.height': number;
        /** (read-only) Returns the width of the bitmap. */
        readonly 'pointStyle.bitmap.width': number;
        /** (read-only) Returns the size of the point used for click detection. */
        readonly 'pointStyle.clickSize': number;
        /** (read-only) Returns the color of the vector element. */
        readonly 'pointStyle.color': number;
        /** (read-only) Returns the size of the point. */
        readonly 'pointStyle.size': number;
        /** Returns the polygon style. */
        'polygonStyle': Handle;
        /** (read-only) Returns the color of the vector element. */
        readonly 'polygonStyle.color': number;
        /** (read-only) Returns the style of the edges of the polygon. */
        readonly 'polygonStyle.lineStyle': Handle;
        /** (read-only) Returns the bitmap of the line. */
        readonly 'polygonStyle.lineStyle.bitmap': Handle;
        /** (read-only) Returns the width of the line used for click detection. */
        readonly 'polygonStyle.lineStyle.clickWidth': number;
        /** (read-only) Returns the color of the vector element. */
        readonly 'polygonStyle.lineStyle.color': number;
        /** (read-only) Returns the end point type of the line. */
        readonly 'polygonStyle.lineStyle.lineEndType': 'LINE_END_TYPE_NONE' | 'LINE_END_TYPE_SQUARE' | 'LINE_END_TYPE_ROUND' | number;
        /** (read-only) Returns the join type of the line. */
        readonly 'polygonStyle.lineStyle.lineJoinType': 'LINE_JOIN_TYPE_NONE' | 'LINE_JOIN_TYPE_MITER' | 'LINE_JOIN_TYPE_BEVEL' | 'LINE_JOIN_TYPE_ROUND' | number;
        /** (read-only) Returns the stretching factor of the line. */
        readonly 'polygonStyle.lineStyle.stretchFactor': number;
        /** (read-only) Returns the width of the line. */
        readonly 'polygonStyle.lineStyle.width': number;
    };
    'massif::GeometrySimplifier': {
    };
    'massif::HTTPTileDataSource': {
        /** Returns the current set of HTTP headers used. Initially this set is empty and can be changed with setHTTPHeaders. */
        'HTTPHeaders': Record<string, string>;
        /** Returns the current set of HTTP headers used. Initially this set is empty and can be changed with setHTTPHeaders. */
        [key: `HTTPHeaders.${string}`]: string;
        /** Returns true/false based whether the TMS tiling scheme is used. */
        'TMSScheme': boolean;
        /** Returns the base URL template containing tags. */
        'baseURL': string;
        /** (read-only) Returns the extent of the tiles in this data source. The bounds are in coordinate system of the projection of the data source. */
        readonly 'dataExtent': Bounds;
        /** Returns true/false based on whether the max-age header check is used. If this is enabled, SDK will automatically refresh the tiles when tiles have expired. */
        'maxAgeHeaderCheck': boolean;
        /** Gets the current maximum overzoom level for this datasource. Over it the datasource will not be "drawn" */
        'maxOverzoomLevel': number;
        /** (read-only) Returns the maximum zoom level supported by this data source. */
        readonly 'maxZoom': number;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        'metaData': Record<string, Json>;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        [key: `metaData.${string}`]: Json;
        /** (read-only) Returns the minimum zoom level supported by this data source. */
        readonly 'minZoom': number;
        /** (read-only) Returns the projection of this tile source. */
        readonly 'projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'projection.bounds': Bounds;
        readonly 'projection.name': string;
        /** Returns the subdomains for {s} tag. The default is ["a", "b", "c", "d"]. */
        'subdomains': string[];
        /** Returns the current timeout value. */
        'timeout': number;
    };
    'massif::HillshadeRasterTileLayer': {
        /** Returns the tile data source of the associated UTF grid. By default this is null. */
        'UTFGridDataSource': Handle;
        /** (read-only) Returns the extent of the tiles in this data source. The bounds are in coordinate system of the projection of the data source. */
        readonly 'UTFGridDataSource.dataExtent': Bounds;
        /** Gets the current maximum overzoom level for this datasource. Over it the datasource will not be "drawn" */
        'UTFGridDataSource.maxOverzoomLevel': number;
        /** (read-only) Returns the maximum zoom level supported by this data source. */
        readonly 'UTFGridDataSource.maxZoom': number;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        'UTFGridDataSource.metaData': Record<string, Json>;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        [key: `UTFGridDataSource.metaData.${string}`]: Json;
        /** (read-only) Returns the minimum zoom level supported by this data source. */
        readonly 'UTFGridDataSource.minZoom': number;
        /** (read-only) Returns the projection of this tile source. */
        readonly 'UTFGridDataSource.projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'UTFGridDataSource.projection.bounds': Bounds;
        readonly 'UTFGridDataSource.projection.name': string;
        /** Returns the UTF grid event listener. */
        'UTFGridEventListener': Handle;
        /** Returns the shading color used to accentuate rugged terrain like sharp cliffs and gorges. */
        'accentColor': number;
        /** Returns the contour line color. */
        'contourColor': number;
        /** Returns whether GPU contour lines are drawn over the hillshade. */
        'contourEnabled': boolean;
        /** Returns the spacing between contour lines in meters. */
        'contourInterval': number;
        /** Returns the contour line half-width in screen pixels. */
        'contourWidth': number;
        /** Returns the contrast of the hillshade overlay. This is the equivalent of MapLibre's 'hillshade-exaggeration' paint property: it controls the slope response curve and the overall strength of the shading, not the relief itself. */
        'contrast': number;
        /** Returns the culling delay of the layer in milliseconds. */
        'cullDelay': number;
        /** (read-only) Returns the data source assigned to this layer. */
        readonly 'dataSource': Handle;
        /** (read-only) Returns the extent of the tiles in this data source. The bounds are in coordinate system of the projection of the data source. */
        readonly 'dataSource.dataExtent': Bounds;
        /** Gets the current maximum overzoom level for this datasource. Over it the datasource will not be "drawn" */
        'dataSource.maxOverzoomLevel': number;
        /** (read-only) Returns the maximum zoom level supported by this data source. */
        readonly 'dataSource.maxZoom': number;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        'dataSource.metaData': Record<string, Json>;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        [key: `dataSource.metaData.${string}`]: Json;
        /** (read-only) Returns the minimum zoom level supported by this data source. */
        readonly 'dataSource.minZoom': number;
        /** (read-only) Returns the projection of this tile source. */
        readonly 'dataSource.projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'dataSource.projection.bounds': Bounds;
        readonly 'dataSource.projection.name': string;
        /** Returns whether the normal map encodes absolute elevation (so a custom normal-map lighting shader can call getElevation()). */
        'elevationEncodingEnabled': boolean;
        /** Returns the normal vector tile should be exagerated based on the zoom level. */
        'exagerateHeightScaleEnabled': boolean;
        /** Returns the per-frame relief exaggeration factor, i.e. the vertical exaggeration of the slope. Unlike height scale this is a shader uniform applied at render time (no tile re-decode), so it can be animated smoothly. */
        'exaggeration': number;
        /** Returns the current frame number. */
        'frameNr': number;
        /** Returns the height scale of the hillshade overlay. */
        'heightScale': number;
        /** Returns the shading color of areas that faces towards the light source. */
        'highlightColor': number;
        /** Returns the hillshade rendering method. */
        'hillshadeMethod': 'STANDARD' | 'COMBINED' | 'IGOR' | 'MULTIDIRECTIONAL' | 'BASIC' | number;
        /** Returns the illumination direction of the layer. */
        'illuminationDirection': Position;
        /** Returns whether the illumination direction should change with the map rotation. */
        'illuminationMapRotationEnabled': boolean;
        /** Returns whether the legacy (pre-MapLibre-parity) height scale formula is used. */
        'legacyHeightScaleEnabled': boolean;
        /** Gets the current maximum overzoom level for this layer. */
        'maxOverzoomLevel': number;
        /** Gets the current maximum underzoom level for this layer. */
        'maxUnderzoomLevel': number;
        /** Returns a copy of the layer meta data map. The changes you make to this map are NOT reflected in the actual meta data of the layer. */
        'metaData': Record<string, Json>;
        /** Returns a copy of the layer meta data map. The changes you make to this map are NOT reflected in the actual meta data of the layer. */
        [key: `metaData.${string}`]: Json;
        'normalMapLightingShader': string;
        /** Returns the opacity of this layer. */
        'opacity': number;
        /** Returns whether this layer goes through the post-process effect. */
        'postProcessed': boolean;
        /** Returns the state of the preloading flag of this layer. */
        'preloading': boolean;
        readonly 'preloadingTileCount': number;
        /** (read-only) Returns the projection this layer's data is in, which is its data source's. */
        readonly 'projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'projection.bounds': Bounds;
        readonly 'projection.name': string;
        /** Returns the raster tile event listener. */
        'rasterTileEventListener': Handle;
        /** Returns the custom fragment shader source. */
        'shaderSource': string;
        /** Returns the shading color of areas that face away from the light source. */
        'shadowColor': number;
        /** (read-only) Returns the data source assigned to this layer. */
        readonly 'source': Handle;
        /** (read-only) Returns the extent of the tiles in this data source. The bounds are in coordinate system of the projection of the data source. */
        readonly 'source.dataExtent': Bounds;
        /** Gets the current maximum overzoom level for this datasource. Over it the datasource will not be "drawn" */
        'source.maxOverzoomLevel': number;
        /** (read-only) Returns the maximum zoom level supported by this data source. */
        readonly 'source.maxZoom': number;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        'source.metaData': Record<string, Json>;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        [key: `source.metaData.${string}`]: Json;
        /** (read-only) Returns the minimum zoom level supported by this data source. */
        readonly 'source.minZoom': number;
        /** (read-only) Returns the projection of this tile source. */
        readonly 'source.projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'source.projection.bounds': Bounds;
        readonly 'source.projection.name': string;
        /** Returns the state of the synchronized refresh flag. */
        'synchronizedRefresh': boolean;
        /** Returns whether the layer may shade the 3D terrain's own elevation texture instead of loading a DEM tile set of its own. */
        'terrainPaintEnabled': boolean;
        /** Returns whether the terrain paint shades from the elevation source's own maximum zoom. */
        'terrainPaintFullDetailEnabled': boolean;
        /** Returns the tile texture cache capacity. */
        'textureCacheCapacity': number;
        /** Returns the current relative tile blending speed. */
        'tileBlendingSpeed': number;
        /** Returns the current tile filter mode. */
        'tileFilterMode': 'RASTER_TILE_FILTER_MODE_NEAREST' | 'RASTER_TILE_FILTER_MODE_BILINEAR' | 'RASTER_TILE_FILTER_MODE_BICUBIC' | number;
        /** Returns the tile load listener. */
        'tileLoadListener': Handle;
        /** Returns the current tile substitution policy. */
        'tileSubstitutionPolicy': 'TILE_SUBSTITUTION_POLICY_ALL' | 'TILE_SUBSTITUTION_POLICY_VISIBLE' | 'TILE_SUBSTITUTION_POLICY_NONE' | number;
        /** Returns the layer task priority of this layer. */
        'updatePriority': number;
        /** Returns the visibility of this layer. */
        'visible': boolean;
        /** (read-only) How many tiles the last cull put on screen. A diagnostic: it is what the tile LOD numbers actually cost. */
        readonly 'visibleTileCount': number;
        /** Returns the visible zoom range of this layer. */
        'visibleZoomRange': [number, number];
        /** Gets the current zoom level bias for this layer. */
        'zoomLevelBias': number;
    };
    'massif::Label': {
        /** Returns the base billboard this billboard is attached to. */
        'baseBillboard': Handle;
        /** Returns the base billboard this billboard is attached to. */
        'baseBillboard.baseBillboard': Handle;
        /** (read-only) Returns the bounds of this billboard or the base billboard, if there is one. */
        readonly 'baseBillboard.bounds': Bounds;
        /** Returns the geometry object that defines the location of this billboard. */
        'baseBillboard.geometry': Handle;
        /** (read-only) Returns the minimal bounds for the geometry. */
        readonly 'baseBillboard.geometry.bounds': Bounds;
        readonly 'baseBillboard.geometry.centerPos': Position;
        /** (read-only) Returns the geometry as a GeoJSON string, in its own coordinates. Here rather than only on Feature because serialising a shape otherwise means constructing a GeoJSONGeometryWriter, which no string-based binding can do. */
        readonly 'baseBillboard.geometry.geoJSON': string;
        readonly 'baseBillboard.geometry.type': 'GEOMETRY_TYPE_POINT' | 'GEOMETRY_TYPE_LINE' | 'GEOMETRY_TYPE_POLYGON' | 'GEOMETRY_TYPE_MULTIPOINT' | 'GEOMETRY_TYPE_MULTILINE' | 'GEOMETRY_TYPE_MULTIPOLYGON' | 'GEOMETRY_TYPE_COLLECTION' | number;
        /** Returns the internal id of this vector element. */
        'baseBillboard.id': number;
        /** Returns a copy of the vector element meta data map. The changes you make to this map are NOT reflected in the actual meta data of the element. */
        'baseBillboard.metaData': Record<string, Json>;
        /** Returns a copy of the vector element meta data map. The changes you make to this map are NOT reflected in the actual meta data of the element. */
        [key: `baseBillboard.metaData.${string}`]: Json;
        /** (read-only) Returns the location of the root billboard: getGeometry() if this billboard has a location, otherwise the location found by following the chain of base billboards to its root. */
        readonly 'baseBillboard.rootGeometry': Handle;
        /** (read-only) Returns the minimal bounds for the geometry. */
        readonly 'baseBillboard.rootGeometry.bounds': Bounds;
        readonly 'baseBillboard.rootGeometry.centerPos': Position;
        /** (read-only) Returns the geometry as a GeoJSON string, in its own coordinates. Here rather than only on Feature because serialising a shape otherwise means constructing a GeoJSONGeometryWriter, which no string-based binding can do. */
        readonly 'baseBillboard.rootGeometry.geoJSON': string;
        readonly 'baseBillboard.rootGeometry.type': 'GEOMETRY_TYPE_POINT' | 'GEOMETRY_TYPE_LINE' | 'GEOMETRY_TYPE_POLYGON' | 'GEOMETRY_TYPE_MULTIPOINT' | 'GEOMETRY_TYPE_MULTILINE' | 'GEOMETRY_TYPE_MULTIPOLYGON' | 'GEOMETRY_TYPE_COLLECTION' | number;
        /** Returns the rotation angle of this billboard. */
        'baseBillboard.rotation': number;
        /** Returns the state of the visibility flag of this vector element. */
        'baseBillboard.visible': boolean;
        /** (read-only) Returns the bounds of this billboard or the base billboard, if there is one. */
        readonly 'bounds': Bounds;
        /** Returns the geometry object that defines the location of this billboard. */
        'geometry': Handle;
        /** (read-only) Returns the minimal bounds for the geometry. */
        readonly 'geometry.bounds': Bounds;
        readonly 'geometry.centerPos': Position;
        /** (read-only) Returns the geometry as a GeoJSON string, in its own coordinates. Here rather than only on Feature because serialising a shape otherwise means constructing a GeoJSONGeometryWriter, which no string-based binding can do. */
        readonly 'geometry.geoJSON': string;
        readonly 'geometry.type': 'GEOMETRY_TYPE_POINT' | 'GEOMETRY_TYPE_LINE' | 'GEOMETRY_TYPE_POLYGON' | 'GEOMETRY_TYPE_MULTIPOINT' | 'GEOMETRY_TYPE_MULTILINE' | 'GEOMETRY_TYPE_MULTIPOLYGON' | 'GEOMETRY_TYPE_COLLECTION' | number;
        /** Returns the internal id of this vector element. */
        'id': number;
        /** Returns a copy of the vector element meta data map. The changes you make to this map are NOT reflected in the actual meta data of the element. */
        'metaData': Record<string, Json>;
        /** Returns a copy of the vector element meta data map. The changes you make to this map are NOT reflected in the actual meta data of the element. */
        [key: `metaData.${string}`]: Json;
        /** (read-only) Returns the location of the root billboard: getGeometry() if this billboard has a location, otherwise the location found by following the chain of base billboards to its root. */
        readonly 'rootGeometry': Handle;
        /** (read-only) Returns the minimal bounds for the geometry. */
        readonly 'rootGeometry.bounds': Bounds;
        readonly 'rootGeometry.centerPos': Position;
        /** (read-only) Returns the geometry as a GeoJSON string, in its own coordinates. Here rather than only on Feature because serialising a shape otherwise means constructing a GeoJSONGeometryWriter, which no string-based binding can do. */
        readonly 'rootGeometry.geoJSON': string;
        readonly 'rootGeometry.type': 'GEOMETRY_TYPE_POINT' | 'GEOMETRY_TYPE_LINE' | 'GEOMETRY_TYPE_POLYGON' | 'GEOMETRY_TYPE_MULTIPOINT' | 'GEOMETRY_TYPE_MULTILINE' | 'GEOMETRY_TYPE_MULTIPOLYGON' | 'GEOMETRY_TYPE_COLLECTION' | number;
        /** Returns the rotation angle of this billboard. */
        'rotation': number;
        /** Returns the style of this label. */
        'style': Handle;
        /** (read-only) Returns the horizontal anchor point of the label. */
        readonly 'style.anchorPointX': number;
        /** (read-only) Returns the vertical anchor point of the label. */
        readonly 'style.anchorPointY': number;
        /** (read-only) Returns the animation style of the billboard. */
        readonly 'style.animationStyle': Handle;
        /** (read-only) Returns the fade animation type. */
        readonly 'style.animationStyle.fadeAnimationType': 'ANIMATION_TYPE_NONE' | 'ANIMATION_TYPE_STEP' | 'ANIMATION_TYPE_LINEAR' | 'ANIMATION_TYPE_SMOOTHSTEP' | 'ANIMATION_TYPE_SPRING' | number;
        /** (read-only) Returns the phase-in duration of the animation. */
        readonly 'style.animationStyle.phaseInDuration': number;
        /** (read-only) Returns the phase-out duration of the animation. */
        readonly 'style.animationStyle.phaseOutDuration': number;
        /** (read-only) Returns the relative speed of the animation. */
        readonly 'style.animationStyle.relativeSpeed': number;
        /** (read-only) Returns the size-related animation type. */
        readonly 'style.animationStyle.sizeAnimationType': 'ANIMATION_TYPE_NONE' | 'ANIMATION_TYPE_STEP' | 'ANIMATION_TYPE_LINEAR' | 'ANIMATION_TYPE_SMOOTHSTEP' | 'ANIMATION_TYPE_SPRING' | number;
        /** (read-only) Returns the horizontal attaching anchor point of the billoard. */
        readonly 'style.attachAnchorPointX': number;
        /** (read-only) Returns the vertical attaching anchor point of the billoard. */
        readonly 'style.attachAnchorPointY': number;
        /** (read-only) Returns the state of the causes overlap flag. */
        readonly 'style.causesOverlap': boolean;
        /** (read-only) Returns the color of the vector element. */
        readonly 'style.color': number;
        /** (read-only) Returns the state of the flippable flag. */
        readonly 'style.flippable': boolean;
        /** (read-only) Returns the state of the allow overlap flag. */
        readonly 'style.hideIfOverlapped': boolean;
        /** (read-only) Returns the horizontal offset of the billboard. */
        readonly 'style.horizontalOffset': number;
        /** (read-only) Returns the orientation mode of the label. */
        readonly 'style.orientationMode': 'BILLBOARD_ORIENTATION_FACE_CAMERA' | 'BILLBOARD_ORIENTATION_FACE_CAMERA_GROUND' | 'BILLBOARD_ORIENTATION_GROUND' | number;
        /** (read-only) Returns the placement priority of the billboard. */
        readonly 'style.placementPriority': number;
        /** (read-only) Returns the relative rendering scale of the label. */
        readonly 'style.renderScale': number;
        /** (read-only) Returns the state of the scale with DPI flag. */
        readonly 'style.scaleWithDPI': boolean;
        /** (read-only) Returns the scaling mode of the label. */
        readonly 'style.scalingMode': 'BILLBOARD_SCALING_WORLD_SIZE' | 'BILLBOARD_SCALING_SCREEN_SIZE' | 'BILLBOARD_SCALING_CONST_SCREEN_SIZE' | number;
        /** (read-only) Returns the vertical offset of the billboard. */
        readonly 'style.verticalOffset': number;
        /** Returns the state of the visibility flag of this vector element. */
        'visible': boolean;
    };
    'massif::LabelStyle': {
        /** (read-only) Returns the horizontal anchor point of the label. */
        readonly 'anchorPointX': number;
        /** (read-only) Returns the vertical anchor point of the label. */
        readonly 'anchorPointY': number;
        /** (read-only) Returns the animation style of the billboard. */
        readonly 'animationStyle': Handle;
        /** (read-only) Returns the fade animation type. */
        readonly 'animationStyle.fadeAnimationType': 'ANIMATION_TYPE_NONE' | 'ANIMATION_TYPE_STEP' | 'ANIMATION_TYPE_LINEAR' | 'ANIMATION_TYPE_SMOOTHSTEP' | 'ANIMATION_TYPE_SPRING' | number;
        /** (read-only) Returns the phase-in duration of the animation. */
        readonly 'animationStyle.phaseInDuration': number;
        /** (read-only) Returns the phase-out duration of the animation. */
        readonly 'animationStyle.phaseOutDuration': number;
        /** (read-only) Returns the relative speed of the animation. */
        readonly 'animationStyle.relativeSpeed': number;
        /** (read-only) Returns the size-related animation type. */
        readonly 'animationStyle.sizeAnimationType': 'ANIMATION_TYPE_NONE' | 'ANIMATION_TYPE_STEP' | 'ANIMATION_TYPE_LINEAR' | 'ANIMATION_TYPE_SMOOTHSTEP' | 'ANIMATION_TYPE_SPRING' | number;
        /** (read-only) Returns the horizontal attaching anchor point of the billoard. */
        readonly 'attachAnchorPointX': number;
        /** (read-only) Returns the vertical attaching anchor point of the billoard. */
        readonly 'attachAnchorPointY': number;
        /** (read-only) Returns the state of the causes overlap flag. */
        readonly 'causesOverlap': boolean;
        /** (read-only) Returns the color of the vector element. */
        readonly 'color': number;
        /** (read-only) Returns the state of the flippable flag. */
        readonly 'flippable': boolean;
        /** (read-only) Returns the state of the allow overlap flag. */
        readonly 'hideIfOverlapped': boolean;
        /** (read-only) Returns the horizontal offset of the billboard. */
        readonly 'horizontalOffset': number;
        /** (read-only) Returns the orientation mode of the label. */
        readonly 'orientationMode': 'BILLBOARD_ORIENTATION_FACE_CAMERA' | 'BILLBOARD_ORIENTATION_FACE_CAMERA_GROUND' | 'BILLBOARD_ORIENTATION_GROUND' | number;
        /** (read-only) Returns the placement priority of the billboard. */
        readonly 'placementPriority': number;
        /** (read-only) Returns the relative rendering scale of the label. */
        readonly 'renderScale': number;
        /** (read-only) Returns the state of the scale with DPI flag. */
        readonly 'scaleWithDPI': boolean;
        /** (read-only) Returns the scaling mode of the label. */
        readonly 'scalingMode': 'BILLBOARD_SCALING_WORLD_SIZE' | 'BILLBOARD_SCALING_SCREEN_SIZE' | 'BILLBOARD_SCALING_CONST_SCREEN_SIZE' | number;
        /** (read-only) Returns the vertical offset of the billboard. */
        readonly 'verticalOffset': number;
    };
    'massif::LabelStyleBuilder': {
        /** Returns the horizontal anchor point of the label. */
        'anchorPointX': number;
        /** Returns the vertical anchor point of the label. */
        'anchorPointY': number;
        /** Returns the animation style of the billboard. */
        'animationStyle': Handle;
        /** (read-only) Returns the fade animation type. */
        readonly 'animationStyle.fadeAnimationType': 'ANIMATION_TYPE_NONE' | 'ANIMATION_TYPE_STEP' | 'ANIMATION_TYPE_LINEAR' | 'ANIMATION_TYPE_SMOOTHSTEP' | 'ANIMATION_TYPE_SPRING' | number;
        /** (read-only) Returns the phase-in duration of the animation. */
        readonly 'animationStyle.phaseInDuration': number;
        /** (read-only) Returns the phase-out duration of the animation. */
        readonly 'animationStyle.phaseOutDuration': number;
        /** (read-only) Returns the relative speed of the animation. */
        readonly 'animationStyle.relativeSpeed': number;
        /** (read-only) Returns the size-related animation type. */
        readonly 'animationStyle.sizeAnimationType': 'ANIMATION_TYPE_NONE' | 'ANIMATION_TYPE_STEP' | 'ANIMATION_TYPE_LINEAR' | 'ANIMATION_TYPE_SMOOTHSTEP' | 'ANIMATION_TYPE_SPRING' | number;
        /** Returns the horizontal attaching anchor point of the billboard. */
        'attachAnchorPointX': number;
        /** Returns the vertical attaching anchor point of the billboard. */
        'attachAnchorPointY': number;
        /** Returns the state of the causes overlap flag. */
        'causesOverlap': boolean;
        /** Returns the color of the vector element. */
        'color': number;
        /** Returns the state of the flippable flag. */
        'flippable': boolean;
        /** Returns the state of the allow overlap flag. */
        'hideIfOverlapped': boolean;
        /** Returns the horizontal offset of the billboard. */
        'horizontalOffset': number;
        /** Returns the orientation mode of the label. */
        'orientationMode': 'BILLBOARD_ORIENTATION_FACE_CAMERA' | 'BILLBOARD_ORIENTATION_FACE_CAMERA_GROUND' | 'BILLBOARD_ORIENTATION_GROUND' | number;
        /** Returns the placement priority of the billboard. */
        'placementPriority': number;
        /** Returns the relative rendering scale for the label. */
        'renderScale': number;
        /** Returns the state of the scale with DPI flag. */
        'scaleWithDPI': boolean;
        /** Returns the scaling mode of the label. */
        'scalingMode': 'BILLBOARD_SCALING_WORLD_SIZE' | 'BILLBOARD_SCALING_SCREEN_SIZE' | 'BILLBOARD_SCALING_CONST_SCREEN_SIZE' | number;
        /** Returns the vertical offset of the billboard. */
        'verticalOffset': number;
    };
    'massif::Layer': {
        /** Returns the culling delay of the layer in milliseconds. */
        'cullDelay': number;
        /** Returns a copy of the layer meta data map. The changes you make to this map are NOT reflected in the actual meta data of the layer. */
        'metaData': Record<string, Json>;
        /** Returns a copy of the layer meta data map. The changes you make to this map are NOT reflected in the actual meta data of the layer. */
        [key: `metaData.${string}`]: Json;
        /** Returns the opacity of this layer. */
        'opacity': number;
        /** Returns whether this layer goes through the post-process effect. */
        'postProcessed': boolean;
        /** Returns the layer task priority of this layer. */
        'updatePriority': number;
        /** Returns the visibility of this layer. */
        'visible': boolean;
        /** Returns the visible zoom range of this layer. */
        'visibleZoomRange': [number, number];
    };
    'massif::Layers': {
        /** (read-only) Returns the current layer count. */
        readonly 'count': number;
    };
    'massif::LightOptions': {
        /** Returns the ambient light color. */
        'ambientColor': number;
        /** Returns the ambient light intensity. */
        'ambientIntensity': number;
        /** Returns the day-cycle light curve. */
        'dayCycleLightStops': Json;
        /** Returns whether the sun's COLOURS follow its position. */
        'dayCycleLightsEnabled': boolean;
        /** Returns the curve used while the sun is RISING, if the app set one. */
        'dayCycleRisingLightStops': Json;
        /** Returns the shadow depth bias scale. */
        'shadowBias': number;
        /** Returns the number of shadow cascades. */
        'shadowCascades': number;
        /** Returns the shadow caster margin in tiles. */
        'shadowCasterMargin': number;
        /** Returns the shadow distance. */
        'shadowDistance': number;
        /** Returns the shadow map resolution. */
        'shadowMapSize': number;
        /** Returns the shadow normal offset. */
        'shadowNormalOffset': number;
        /** Returns the shadow softness. */
        'shadowSoftness': number;
        /** Returns the shadow strength. */
        'shadowStrength': number;
        /** Returns the sun altitude in degrees above the horizon. */
        'sunAltitude': number;
        /** Returns the sun azimuth in degrees. */
        'sunAzimuth': number;
        /** Returns the sun (directional light) color. */
        'sunColor': number;
        /** Returns the sun light intensity. */
        'sunIntensity': number;
        /** Returns whether this sun overrides the one a style states. */
        'sunOverridingStyle': boolean;
        /** Returns whether the sun lights the 3D terrain surface. */
        'terrainLightingEnabled': boolean;
    };
    'massif::LightStop': {
        /** (read-only) Returns the ambient colour. */
        readonly 'ambientColor': number;
        /** (read-only) Returns the ambient intensity. */
        readonly 'ambientIntensity': number;
        /** (read-only) Returns the sun height this light belongs to. */
        readonly 'sunAltitude': number;
        /** (read-only) Returns the directional colour. */
        readonly 'sunColor': number;
        /** (read-only) Returns the directional intensity. */
        readonly 'sunIntensity': number;
    };
    'massif::Line': {
        /** (read-only) Returns the bounds of this vector element. */
        readonly 'bounds': Bounds;
        'geometry': Handle;
        /** (read-only) Returns the minimal bounds for the geometry. */
        readonly 'geometry.bounds': Bounds;
        readonly 'geometry.centerPos': Position;
        /** (read-only) Returns the geometry as a GeoJSON string, in its own coordinates. Here rather than only on Feature because serialising a shape otherwise means constructing a GeoJSONGeometryWriter, which no string-based binding can do. */
        readonly 'geometry.geoJSON': string;
        /** (read-only) Returns the list of of map positions defining the line. */
        readonly 'geometry.poses': Json;
        readonly 'geometry.type': 'GEOMETRY_TYPE_POINT' | 'GEOMETRY_TYPE_LINE' | 'GEOMETRY_TYPE_POLYGON' | 'GEOMETRY_TYPE_MULTIPOINT' | 'GEOMETRY_TYPE_MULTILINE' | 'GEOMETRY_TYPE_MULTIPOLYGON' | 'GEOMETRY_TYPE_COLLECTION' | number;
        /** Returns the internal id of this vector element. */
        'id': number;
        /** Returns a copy of the vector element meta data map. The changes you make to this map are NOT reflected in the actual meta data of the element. */
        'metaData': Record<string, Json>;
        /** Returns a copy of the vector element meta data map. The changes you make to this map are NOT reflected in the actual meta data of the element. */
        [key: `metaData.${string}`]: Json;
        /** Returns the style of this line. */
        'style': Handle;
        /** (read-only) Returns the bitmap of the line. */
        readonly 'style.bitmap': Handle;
        /** (read-only) Returns the bytes per pixel parameter of this bitmap. Valid values are 1, 2, 3 and 4. */
        readonly 'style.bitmap.bytesPerPixel': number;
        /** (read-only) Returns the color format of this bitmap. */
        readonly 'style.bitmap.colorFormat': 'COLOR_FORMAT_UNSUPPORTED' | 'COLOR_FORMAT_BGRA' | 'COLOR_FORMAT_RGBA_4444' | 'COLOR_FORMAT_RGB_565' | number;
        /** (read-only) Returns the height of the bitmap. */
        readonly 'style.bitmap.height': number;
        /** (read-only) Returns the width of the bitmap. */
        readonly 'style.bitmap.width': number;
        /** (read-only) Returns the width of the line used for click detection. */
        readonly 'style.clickWidth': number;
        /** (read-only) Returns the color of the vector element. */
        readonly 'style.color': number;
        /** (read-only) Returns the end point type of the line. */
        readonly 'style.lineEndType': 'LINE_END_TYPE_NONE' | 'LINE_END_TYPE_SQUARE' | 'LINE_END_TYPE_ROUND' | number;
        /** (read-only) Returns the join type of the line. */
        readonly 'style.lineJoinType': 'LINE_JOIN_TYPE_NONE' | 'LINE_JOIN_TYPE_MITER' | 'LINE_JOIN_TYPE_BEVEL' | 'LINE_JOIN_TYPE_ROUND' | number;
        /** (read-only) Returns the stretching factor of the line. */
        readonly 'style.stretchFactor': number;
        /** (read-only) Returns the width of the line. */
        readonly 'style.width': number;
        /** Returns the state of the visibility flag of this vector element. */
        'visible': boolean;
    };
    'massif::LineGeometry': {
        /** (read-only) Returns the minimal bounds for the geometry. */
        readonly 'bounds': Bounds;
        readonly 'centerPos': Position;
        /** (read-only) Returns the geometry as a GeoJSON string, in its own coordinates. Here rather than only on Feature because serialising a shape otherwise means constructing a GeoJSONGeometryWriter, which no string-based binding can do. */
        readonly 'geoJSON': string;
        /** (read-only) Returns the list of of map positions defining the line. */
        readonly 'poses': Json;
        readonly 'type': 'GEOMETRY_TYPE_POINT' | 'GEOMETRY_TYPE_LINE' | 'GEOMETRY_TYPE_POLYGON' | 'GEOMETRY_TYPE_MULTIPOINT' | 'GEOMETRY_TYPE_MULTILINE' | 'GEOMETRY_TYPE_MULTIPOLYGON' | 'GEOMETRY_TYPE_COLLECTION' | number;
    };
    'massif::LineStyle': {
        /** (read-only) Returns the bitmap of the line. */
        readonly 'bitmap': Handle;
        /** (read-only) Returns the bytes per pixel parameter of this bitmap. Valid values are 1, 2, 3 and 4. */
        readonly 'bitmap.bytesPerPixel': number;
        /** (read-only) Returns the color format of this bitmap. */
        readonly 'bitmap.colorFormat': 'COLOR_FORMAT_UNSUPPORTED' | 'COLOR_FORMAT_BGRA' | 'COLOR_FORMAT_RGBA_4444' | 'COLOR_FORMAT_RGB_565' | number;
        /** (read-only) Returns the height of the bitmap. */
        readonly 'bitmap.height': number;
        /** (read-only) Returns the width of the bitmap. */
        readonly 'bitmap.width': number;
        /** (read-only) Returns the width of the line used for click detection. */
        readonly 'clickWidth': number;
        /** (read-only) Returns the color of the vector element. */
        readonly 'color': number;
        /** (read-only) Returns the end point type of the line. */
        readonly 'lineEndType': 'LINE_END_TYPE_NONE' | 'LINE_END_TYPE_SQUARE' | 'LINE_END_TYPE_ROUND' | number;
        /** (read-only) Returns the join type of the line. */
        readonly 'lineJoinType': 'LINE_JOIN_TYPE_NONE' | 'LINE_JOIN_TYPE_MITER' | 'LINE_JOIN_TYPE_BEVEL' | 'LINE_JOIN_TYPE_ROUND' | number;
        /** (read-only) Returns the stretching factor of the line. */
        readonly 'stretchFactor': number;
        /** (read-only) Returns the width of the line. */
        readonly 'width': number;
    };
    'massif::LineStyleBuilder': {
        /** Returns the bitmap of the line. */
        'bitmap': Handle;
        /** (read-only) Returns the bytes per pixel parameter of this bitmap. Valid values are 1, 2, 3 and 4. */
        readonly 'bitmap.bytesPerPixel': number;
        /** (read-only) Returns the color format of this bitmap. */
        readonly 'bitmap.colorFormat': 'COLOR_FORMAT_UNSUPPORTED' | 'COLOR_FORMAT_BGRA' | 'COLOR_FORMAT_RGBA_4444' | 'COLOR_FORMAT_RGB_565' | number;
        /** (read-only) Returns the height of the bitmap. */
        readonly 'bitmap.height': number;
        /** (read-only) Returns the width of the bitmap. */
        readonly 'bitmap.width': number;
        /** Returns the width of the line used for click detection. */
        'clickWidth': number;
        /** Returns the color of the vector element. */
        'color': number;
        /** Returns the end point type of the line. */
        'lineEndType': 'LINE_END_TYPE_NONE' | 'LINE_END_TYPE_SQUARE' | 'LINE_END_TYPE_ROUND' | number;
        /** Returns the join type of the line. */
        'lineJoinType': 'LINE_JOIN_TYPE_NONE' | 'LINE_JOIN_TYPE_MITER' | 'LINE_JOIN_TYPE_BEVEL' | 'LINE_JOIN_TYPE_ROUND' | number;
        /** Returns the stretch factor of the line. */
        'stretchFactor': number;
        /** Returns the width of the line. */
        'width': number;
    };
    'massif::LocalVectorDataSource': {
        /** (read-only) Returns the extent of the data of this data source. The bounds are in coordinate system of the projection of the data source. */
        readonly 'dataExtent': Bounds;
        /** Returns the active geometry simplifier of the data source. */
        'geometrySimplifier': Handle;
        /** (read-only) Returns the projection used by this data source. */
        readonly 'projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'projection.bounds': Bounds;
        readonly 'projection.name': string;
    };
    'massif::Log': {
        /** Returns the current log listener. */
        'logEventListener': Handle;
        /** Returns the state of internal debug message logging. */
        'showDebug': boolean;
        /** Returns the state of error logging. */
        'showError': boolean;
        /** Returns the state of general info logging. */
        'showInfo': boolean;
        /** Returns the state of warning logging. */
        'showWarn': boolean;
        /** Returns the tag for the log events. */
        'tag': string;
    };
    'massif::LogEventListener': {
    };
    'massif::MBTilesTileDataSource': {
        /** (read-only) Returns the extent of the tiles in this data source. The bounds are in coordinate system of the projection of the data source. */
        readonly 'dataExtent': Bounds;
        /** Gets the current maximum overzoom level for this datasource. Over it the datasource will not be "drawn" */
        'maxOverzoomLevel': number;
        /** (read-only) Returns the maximum zoom level supported by this data source. */
        readonly 'maxZoom': number;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        'metaData': Record<string, Json>;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        [key: `metaData.${string}`]: Json;
        /** (read-only) Returns the minimum zoom level supported by this data source. */
        readonly 'minZoom': number;
        /** (read-only) Returns the projection of this tile source. */
        readonly 'projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'projection.bounds': Bounds;
        readonly 'projection.name': string;
    };
    'massif::MBVectorTileDecoder': {
        /** Returns the current CartoCSS style set used by the decoder. If decoder uses non-CartoCSS style set, null is returned. */
        'cartoCSSStyle': Handle;
        /** (read-only) Returns the style asset package. */
        readonly 'cartoCSSStyle.assetPackage': Handle;
        readonly 'cartoCSSStyle.assetPackage.assetNames': string[];
        /** (read-only) Returns the CartoCSS string used for the style. */
        readonly 'cartoCSSStyle.cartoCSS': string;
        /** Returns the current compiled style set used by the decoder. If decoder uses non-compiled style set, null is returned. */
        'compiledStyle': Handle;
        /** (read-only) Returns the style asset package. */
        readonly 'compiledStyle.assetPackage': Handle;
        readonly 'compiledStyle.assetPackage.assetNames': string[];
        /** (read-only) Returns the asset name defining the current style name. */
        readonly 'compiledStyle.styleAssetName': string;
        /** (read-only) Returns the current style name. */
        readonly 'compiledStyle.styleName': string;
        /** Returns the value of feature id override flag. This is intended for cases when feature ids in tile are not globally unique. */
        'featureIdOverride': boolean;
        readonly 'maxZoom': number;
        readonly 'minZoom': number;
        /** Returns the value of the specified style parameter. The style parameter must be declared in the current style. */
        'params': Record<string, string>;
        /** Returns the value of the specified style parameter. The style parameter must be declared in the current style. */
        [key: `params.${string}`]: string;
        /** (read-only) Returns the ordered style layer names as declared by the style (project JSON "layers", or Mapnik XML Layers), i.e. the draw order. CompositeVectorTileLayer places external sources by it: a source whose name is not in this list is not drawn. */
        readonly 'styleLayerNames': string[];
        /** (read-only) Returns the list of all available style parameters. */
        readonly 'styleParameters': string[];
        /** Returns the binary format the tiles are decoded as. */
        'tileFormat': 'TILE_FORMAT_AUTO' | 'TILE_FORMAT_MVT' | 'TILE_FORMAT_MLT' | number;
    };
    'massif::ManeuverArrowBuilder': {
        /** Returns the length of the arrow after the maneuver point. */
        'lengthAfter': number;
        /** Returns the length of the arrow before the maneuver point. */
        'lengthBefore': number;
    };
    'massif::MapBounds': {
        /** (read-only) Calculates the center map position of this map envelope object. */
        readonly 'center': Position;
        /** (read-only) Calculates the difference vector between the maximum and minimum map positions of this map bounds object. */
        readonly 'delta': Position;
        /** (read-only) Returns the maximum (north east) map position of this map envelope object. */
        readonly 'max': Position;
        /** (read-only) Returns the minimum (south west) map position of this map envelope object. */
        readonly 'min': Position;
    };
    'massif::MapBoxElevationDataDecoder': {
    };
    'massif::MapBoxOnlineGeocodingService': {
        'autocomplete': boolean;
        /** Returns the custom backend service URL. */
        'customServiceURL': string;
        'language': string;
        'maxResults': number;
    };
    'massif::MapBoxOnlineReverseGeocodingService': {
        /** Returns the custom backend service URL. */
        'customServiceURL': string;
        'language': string;
    };
    'massif::MapClickInfo': {
        /** (read-only) Returns the click info. */
        readonly 'clickInfo': ClickInfo;
        /** (read-only) Returns the click position. */
        readonly 'clickPos': Position;
        /** (read-only) Returns the click type. */
        readonly 'clickType': 'CLICK_TYPE_SINGLE' | 'CLICK_TYPE_LONG' | 'CLICK_TYPE_DOUBLE' | 'CLICK_TYPE_DUAL' | number;
    };
    'massif::MapEnvelope': {
        /** (read-only) Returns the map bounds of this map envelope. */
        readonly 'bounds': Bounds;
        /** (read-only) Returns the convex hull of this map envelope. */
        readonly 'convexHull': Json;
    };
    'massif::MapEventListener': {
    };
    'massif::MapInteractionInfo': {
        /** (read-only) Returns true if the interaction has started an animation. */
        readonly 'animationStarted': boolean;
        /** (read-only) Returns true if the interaction included a map pan action. */
        readonly 'panAction': boolean;
        /** (read-only) Returns true if the interaction included a rotate action. */
        readonly 'rotateAction': boolean;
        /** (read-only) Returns true if the interaction included a tilt action. */
        readonly 'tiltAction': boolean;
        /** (read-only) Returns true if the interaction included a zoom action. */
        readonly 'zoomAction': boolean;
    };
    'massif::MapMoveInfo': {
        /** (read-only) Returns what caused the movement. */
        readonly 'reason': 'MAP_MOVE_REASON_GESTURE' | 'MAP_MOVE_REASON_ANIMATION' | 'MAP_MOVE_REASON_API' | number;
    };
    'massif::MapPos': {
        /** (read-only) Returns the x coordinate of this map position. */
        readonly 'x': number;
        /** (read-only) Returns the y coordinate of this map position. */
        readonly 'y': number;
        /** (read-only) Returns the z coordinate of this map position. */
        readonly 'z': number;
    };
    'massif::MapRange': {
        /** (read-only) Calculates the length of this map range. Defined as max - min. */
        readonly 'length': number;
        /** (read-only) Returns the max value of this map range. */
        readonly 'max': number;
        /** (read-only) Calculate the midrange value. */
        readonly 'midrange': number;
        /** (read-only) Returns the min value of this map range. */
        readonly 'min': number;
    };
    'massif::MapRenderer': {
        /** Returns the map renderer listener. Can be null. */
        'mapRendererListener': Handle;
        /** Returns the current post-process effect. Can be null. */
        'postProcessEffect': Handle;
        /** (read-only) Returns the fragment shader source of the effect. */
        readonly 'postProcessEffect.fragmentShader': string;
        /** (read-only) Returns the name of the effect. */
        readonly 'postProcessEffect.name': string;
        /** Returns true if the effect needs the terrain depth pre-pass (uTerrainDepthTex). */
        'postProcessEffect.terrainDepthRequired': boolean;
        /** Returns true if the effect wants the terrain surface normal in the depth pre-pass. */
        'postProcessEffect.terrainNormalsRequired': boolean;
    };
    'massif::MapRendererListener': {
    };
    'massif::MapTile': {
        /** (read-only) Returns the time of this map tile. */
        readonly 'frameNr': number;
        /** (read-only) Returns the internal tile id of this map tile. */
        readonly 'tileId': number;
        /** (read-only) Returns the x coordinate of this map tile. */
        readonly 'x': number;
        /** (read-only) Returns the y coordinate of this map tile. */
        readonly 'y': number;
        /** (read-only) Returns the zoom level of this map tile. */
        readonly 'zoom': number;
    };
    'massif::MapTilerOnlineTileDataSource': {
        /** Returns the custom backend service URL. */
        'customServiceURL': string;
        /** (read-only) Returns the extent of the tiles in this data source. The bounds are in coordinate system of the projection of the data source. */
        readonly 'dataExtent': Bounds;
        /** Gets the current maximum overzoom level for this datasource. Over it the datasource will not be "drawn" */
        'maxOverzoomLevel': number;
        /** (read-only) Returns the maximum zoom level supported by this data source. */
        readonly 'maxZoom': number;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        'metaData': Record<string, Json>;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        [key: `metaData.${string}`]: Json;
        /** (read-only) Returns the minimum zoom level supported by this data source. */
        readonly 'minZoom': number;
        /** (read-only) Returns the projection of this tile source. */
        readonly 'projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'projection.bounds': Bounds;
        readonly 'projection.name': string;
        /** Returns the current timeout value. */
        'timeout': number;
    };
    'massif::MapVec': {
        /** (read-only) Calculates the length of this map vector. */
        readonly 'length': number;
        /** (read-only) Creates a new map vector by normalizing this map vector. */
        readonly 'normalized': Position;
        /** (read-only) Returns the x coordinate of this map vector. */
        readonly 'x': number;
        /** (read-only) Returns the y coordinate of this map vector. */
        readonly 'y': number;
        /** (read-only) Returns the z coordinate of this map vector. */
        readonly 'z': number;
    };
    'massif::Marker': {
        /** Returns the base billboard this billboard is attached to. */
        'baseBillboard': Handle;
        /** Returns the base billboard this billboard is attached to. */
        'baseBillboard.baseBillboard': Handle;
        /** (read-only) Returns the bounds of this billboard or the base billboard, if there is one. */
        readonly 'baseBillboard.bounds': Bounds;
        /** Returns the geometry object that defines the location of this billboard. */
        'baseBillboard.geometry': Handle;
        /** (read-only) Returns the minimal bounds for the geometry. */
        readonly 'baseBillboard.geometry.bounds': Bounds;
        readonly 'baseBillboard.geometry.centerPos': Position;
        /** (read-only) Returns the geometry as a GeoJSON string, in its own coordinates. Here rather than only on Feature because serialising a shape otherwise means constructing a GeoJSONGeometryWriter, which no string-based binding can do. */
        readonly 'baseBillboard.geometry.geoJSON': string;
        readonly 'baseBillboard.geometry.type': 'GEOMETRY_TYPE_POINT' | 'GEOMETRY_TYPE_LINE' | 'GEOMETRY_TYPE_POLYGON' | 'GEOMETRY_TYPE_MULTIPOINT' | 'GEOMETRY_TYPE_MULTILINE' | 'GEOMETRY_TYPE_MULTIPOLYGON' | 'GEOMETRY_TYPE_COLLECTION' | number;
        /** Returns the internal id of this vector element. */
        'baseBillboard.id': number;
        /** Returns a copy of the vector element meta data map. The changes you make to this map are NOT reflected in the actual meta data of the element. */
        'baseBillboard.metaData': Record<string, Json>;
        /** Returns a copy of the vector element meta data map. The changes you make to this map are NOT reflected in the actual meta data of the element. */
        [key: `baseBillboard.metaData.${string}`]: Json;
        /** (read-only) Returns the location of the root billboard: getGeometry() if this billboard has a location, otherwise the location found by following the chain of base billboards to its root. */
        readonly 'baseBillboard.rootGeometry': Handle;
        /** (read-only) Returns the minimal bounds for the geometry. */
        readonly 'baseBillboard.rootGeometry.bounds': Bounds;
        readonly 'baseBillboard.rootGeometry.centerPos': Position;
        /** (read-only) Returns the geometry as a GeoJSON string, in its own coordinates. Here rather than only on Feature because serialising a shape otherwise means constructing a GeoJSONGeometryWriter, which no string-based binding can do. */
        readonly 'baseBillboard.rootGeometry.geoJSON': string;
        readonly 'baseBillboard.rootGeometry.type': 'GEOMETRY_TYPE_POINT' | 'GEOMETRY_TYPE_LINE' | 'GEOMETRY_TYPE_POLYGON' | 'GEOMETRY_TYPE_MULTIPOINT' | 'GEOMETRY_TYPE_MULTILINE' | 'GEOMETRY_TYPE_MULTIPOLYGON' | 'GEOMETRY_TYPE_COLLECTION' | number;
        /** Returns the rotation angle of this billboard. */
        'baseBillboard.rotation': number;
        /** Returns the state of the visibility flag of this vector element. */
        'baseBillboard.visible': boolean;
        /** (read-only) Returns the bounds of this billboard or the base billboard, if there is one. */
        readonly 'bounds': Bounds;
        /** Returns the geometry object that defines the location of this billboard. */
        'geometry': Handle;
        /** (read-only) Returns the minimal bounds for the geometry. */
        readonly 'geometry.bounds': Bounds;
        readonly 'geometry.centerPos': Position;
        /** (read-only) Returns the geometry as a GeoJSON string, in its own coordinates. Here rather than only on Feature because serialising a shape otherwise means constructing a GeoJSONGeometryWriter, which no string-based binding can do. */
        readonly 'geometry.geoJSON': string;
        readonly 'geometry.type': 'GEOMETRY_TYPE_POINT' | 'GEOMETRY_TYPE_LINE' | 'GEOMETRY_TYPE_POLYGON' | 'GEOMETRY_TYPE_MULTIPOINT' | 'GEOMETRY_TYPE_MULTILINE' | 'GEOMETRY_TYPE_MULTIPOLYGON' | 'GEOMETRY_TYPE_COLLECTION' | number;
        /** Returns the internal id of this vector element. */
        'id': number;
        /** Returns a copy of the vector element meta data map. The changes you make to this map are NOT reflected in the actual meta data of the element. */
        'metaData': Record<string, Json>;
        /** Returns a copy of the vector element meta data map. The changes you make to this map are NOT reflected in the actual meta data of the element. */
        [key: `metaData.${string}`]: Json;
        /** (read-only) Returns the location of the root billboard: getGeometry() if this billboard has a location, otherwise the location found by following the chain of base billboards to its root. */
        readonly 'rootGeometry': Handle;
        /** (read-only) Returns the minimal bounds for the geometry. */
        readonly 'rootGeometry.bounds': Bounds;
        readonly 'rootGeometry.centerPos': Position;
        /** (read-only) Returns the geometry as a GeoJSON string, in its own coordinates. Here rather than only on Feature because serialising a shape otherwise means constructing a GeoJSONGeometryWriter, which no string-based binding can do. */
        readonly 'rootGeometry.geoJSON': string;
        readonly 'rootGeometry.type': 'GEOMETRY_TYPE_POINT' | 'GEOMETRY_TYPE_LINE' | 'GEOMETRY_TYPE_POLYGON' | 'GEOMETRY_TYPE_MULTIPOINT' | 'GEOMETRY_TYPE_MULTILINE' | 'GEOMETRY_TYPE_MULTIPOLYGON' | 'GEOMETRY_TYPE_COLLECTION' | number;
        /** Returns the rotation angle of this billboard. */
        'rotation': number;
        /** Returns the style of this marker. */
        'style': Handle;
        /** (read-only) Returns the horizontal anchor point of the marker. */
        readonly 'style.anchorPointX': number;
        /** (read-only) Returns the vertical anchor point of the marker. */
        readonly 'style.anchorPointY': number;
        /** (read-only) Returns the animation style of the billboard. */
        readonly 'style.animationStyle': Handle;
        /** (read-only) Returns the fade animation type. */
        readonly 'style.animationStyle.fadeAnimationType': 'ANIMATION_TYPE_NONE' | 'ANIMATION_TYPE_STEP' | 'ANIMATION_TYPE_LINEAR' | 'ANIMATION_TYPE_SMOOTHSTEP' | 'ANIMATION_TYPE_SPRING' | number;
        /** (read-only) Returns the phase-in duration of the animation. */
        readonly 'style.animationStyle.phaseInDuration': number;
        /** (read-only) Returns the phase-out duration of the animation. */
        readonly 'style.animationStyle.phaseOutDuration': number;
        /** (read-only) Returns the relative speed of the animation. */
        readonly 'style.animationStyle.relativeSpeed': number;
        /** (read-only) Returns the size-related animation type. */
        readonly 'style.animationStyle.sizeAnimationType': 'ANIMATION_TYPE_NONE' | 'ANIMATION_TYPE_STEP' | 'ANIMATION_TYPE_LINEAR' | 'ANIMATION_TYPE_SMOOTHSTEP' | 'ANIMATION_TYPE_SPRING' | number;
        /** (read-only) Returns the horizontal attaching anchor point of the billoard. */
        readonly 'style.attachAnchorPointX': number;
        /** (read-only) Returns the vertical attaching anchor point of the billoard. */
        readonly 'style.attachAnchorPointY': number;
        /** (read-only) Returns the bitmap of the marker. */
        readonly 'style.bitmap': Handle;
        /** (read-only) Returns the bytes per pixel parameter of this bitmap. Valid values are 1, 2, 3 and 4. */
        readonly 'style.bitmap.bytesPerPixel': number;
        /** (read-only) Returns the color format of this bitmap. */
        readonly 'style.bitmap.colorFormat': 'COLOR_FORMAT_UNSUPPORTED' | 'COLOR_FORMAT_BGRA' | 'COLOR_FORMAT_RGBA_4444' | 'COLOR_FORMAT_RGB_565' | number;
        /** (read-only) Returns the height of the bitmap. */
        readonly 'style.bitmap.height': number;
        /** (read-only) Returns the width of the bitmap. */
        readonly 'style.bitmap.width': number;
        /** (read-only) Returns the state of the causes overlap flag. */
        readonly 'style.causesOverlap': boolean;
        /** (read-only) Returns the click size of the marker. */
        readonly 'style.clickSize': number;
        /** (read-only) Returns the color of the vector element. */
        readonly 'style.color': number;
        /** (read-only) Returns the state of the allow overlap flag. */
        readonly 'style.hideIfOverlapped': boolean;
        /** (read-only) Returns the horizontal offset of the billboard. */
        readonly 'style.horizontalOffset': number;
        /** (read-only) Returns the orientation mode of the marker. */
        readonly 'style.orientationMode': 'BILLBOARD_ORIENTATION_FACE_CAMERA' | 'BILLBOARD_ORIENTATION_FACE_CAMERA_GROUND' | 'BILLBOARD_ORIENTATION_GROUND' | number;
        /** (read-only) Returns the placement priority of the billboard. */
        readonly 'style.placementPriority': number;
        /** (read-only) Returns the state of the scale with DPI flag. */
        readonly 'style.scaleWithDPI': boolean;
        /** (read-only) Returns the scaling mode of the marker. */
        readonly 'style.scalingMode': 'BILLBOARD_SCALING_WORLD_SIZE' | 'BILLBOARD_SCALING_SCREEN_SIZE' | 'BILLBOARD_SCALING_CONST_SCREEN_SIZE' | number;
        /** (read-only) Returns the size of the marker. */
        readonly 'style.size': number;
        /** (read-only) Returns the vertical offset of the billboard. */
        readonly 'style.verticalOffset': number;
        /** Returns the state of the visibility flag of this vector element. */
        'visible': boolean;
    };
    'massif::MarkerStyle': {
        /** (read-only) Returns the horizontal anchor point of the marker. */
        readonly 'anchorPointX': number;
        /** (read-only) Returns the vertical anchor point of the marker. */
        readonly 'anchorPointY': number;
        /** (read-only) Returns the animation style of the billboard. */
        readonly 'animationStyle': Handle;
        /** (read-only) Returns the fade animation type. */
        readonly 'animationStyle.fadeAnimationType': 'ANIMATION_TYPE_NONE' | 'ANIMATION_TYPE_STEP' | 'ANIMATION_TYPE_LINEAR' | 'ANIMATION_TYPE_SMOOTHSTEP' | 'ANIMATION_TYPE_SPRING' | number;
        /** (read-only) Returns the phase-in duration of the animation. */
        readonly 'animationStyle.phaseInDuration': number;
        /** (read-only) Returns the phase-out duration of the animation. */
        readonly 'animationStyle.phaseOutDuration': number;
        /** (read-only) Returns the relative speed of the animation. */
        readonly 'animationStyle.relativeSpeed': number;
        /** (read-only) Returns the size-related animation type. */
        readonly 'animationStyle.sizeAnimationType': 'ANIMATION_TYPE_NONE' | 'ANIMATION_TYPE_STEP' | 'ANIMATION_TYPE_LINEAR' | 'ANIMATION_TYPE_SMOOTHSTEP' | 'ANIMATION_TYPE_SPRING' | number;
        /** (read-only) Returns the horizontal attaching anchor point of the billoard. */
        readonly 'attachAnchorPointX': number;
        /** (read-only) Returns the vertical attaching anchor point of the billoard. */
        readonly 'attachAnchorPointY': number;
        /** (read-only) Returns the bitmap of the marker. */
        readonly 'bitmap': Handle;
        /** (read-only) Returns the bytes per pixel parameter of this bitmap. Valid values are 1, 2, 3 and 4. */
        readonly 'bitmap.bytesPerPixel': number;
        /** (read-only) Returns the color format of this bitmap. */
        readonly 'bitmap.colorFormat': 'COLOR_FORMAT_UNSUPPORTED' | 'COLOR_FORMAT_BGRA' | 'COLOR_FORMAT_RGBA_4444' | 'COLOR_FORMAT_RGB_565' | number;
        /** (read-only) Returns the height of the bitmap. */
        readonly 'bitmap.height': number;
        /** (read-only) Returns the width of the bitmap. */
        readonly 'bitmap.width': number;
        /** (read-only) Returns the state of the causes overlap flag. */
        readonly 'causesOverlap': boolean;
        /** (read-only) Returns the click size of the marker. */
        readonly 'clickSize': number;
        /** (read-only) Returns the color of the vector element. */
        readonly 'color': number;
        /** (read-only) Returns the state of the allow overlap flag. */
        readonly 'hideIfOverlapped': boolean;
        /** (read-only) Returns the horizontal offset of the billboard. */
        readonly 'horizontalOffset': number;
        /** (read-only) Returns the orientation mode of the marker. */
        readonly 'orientationMode': 'BILLBOARD_ORIENTATION_FACE_CAMERA' | 'BILLBOARD_ORIENTATION_FACE_CAMERA_GROUND' | 'BILLBOARD_ORIENTATION_GROUND' | number;
        /** (read-only) Returns the placement priority of the billboard. */
        readonly 'placementPriority': number;
        /** (read-only) Returns the state of the scale with DPI flag. */
        readonly 'scaleWithDPI': boolean;
        /** (read-only) Returns the scaling mode of the marker. */
        readonly 'scalingMode': 'BILLBOARD_SCALING_WORLD_SIZE' | 'BILLBOARD_SCALING_SCREEN_SIZE' | 'BILLBOARD_SCALING_CONST_SCREEN_SIZE' | number;
        /** (read-only) Returns the size of the marker. */
        readonly 'size': number;
        /** (read-only) Returns the vertical offset of the billboard. */
        readonly 'verticalOffset': number;
    };
    'massif::MarkerStyleBuilder': {
        /** Returns the horizontal anchor point of the marker. */
        'anchorPointX': number;
        /** Returns the vertical anchor point of the marker. */
        'anchorPointY': number;
        /** Returns the animation style of the billboard. */
        'animationStyle': Handle;
        /** (read-only) Returns the fade animation type. */
        readonly 'animationStyle.fadeAnimationType': 'ANIMATION_TYPE_NONE' | 'ANIMATION_TYPE_STEP' | 'ANIMATION_TYPE_LINEAR' | 'ANIMATION_TYPE_SMOOTHSTEP' | 'ANIMATION_TYPE_SPRING' | number;
        /** (read-only) Returns the phase-in duration of the animation. */
        readonly 'animationStyle.phaseInDuration': number;
        /** (read-only) Returns the phase-out duration of the animation. */
        readonly 'animationStyle.phaseOutDuration': number;
        /** (read-only) Returns the relative speed of the animation. */
        readonly 'animationStyle.relativeSpeed': number;
        /** (read-only) Returns the size-related animation type. */
        readonly 'animationStyle.sizeAnimationType': 'ANIMATION_TYPE_NONE' | 'ANIMATION_TYPE_STEP' | 'ANIMATION_TYPE_LINEAR' | 'ANIMATION_TYPE_SMOOTHSTEP' | 'ANIMATION_TYPE_SPRING' | number;
        /** Returns the horizontal attaching anchor point of the billboard. */
        'attachAnchorPointX': number;
        /** Returns the vertical attaching anchor point of the billboard. */
        'attachAnchorPointY': number;
        /** Returns the bitmap of the marker. */
        'bitmap': Handle;
        /** (read-only) Returns the bytes per pixel parameter of this bitmap. Valid values are 1, 2, 3 and 4. */
        readonly 'bitmap.bytesPerPixel': number;
        /** (read-only) Returns the color format of this bitmap. */
        readonly 'bitmap.colorFormat': 'COLOR_FORMAT_UNSUPPORTED' | 'COLOR_FORMAT_BGRA' | 'COLOR_FORMAT_RGBA_4444' | 'COLOR_FORMAT_RGB_565' | number;
        /** (read-only) Returns the height of the bitmap. */
        readonly 'bitmap.height': number;
        /** (read-only) Returns the width of the bitmap. */
        readonly 'bitmap.width': number;
        /** Returns the state of the causes overlap flag. */
        'causesOverlap': boolean;
        /** Returns the size of the marker used for click detection. */
        'clickSize': number;
        /** Returns the color of the vector element. */
        'color': number;
        /** Returns the state of the allow overlap flag. */
        'hideIfOverlapped': boolean;
        /** Returns the horizontal offset of the billboard. */
        'horizontalOffset': number;
        /** Returns the orientation mode of the marker. */
        'orientationMode': 'BILLBOARD_ORIENTATION_FACE_CAMERA' | 'BILLBOARD_ORIENTATION_FACE_CAMERA_GROUND' | 'BILLBOARD_ORIENTATION_GROUND' | number;
        /** Returns the placement priority of the billboard. */
        'placementPriority': number;
        /** Returns the state of the scale with DPI flag. */
        'scaleWithDPI': boolean;
        /** Returns the scaling mode of the marker. */
        'scalingMode': 'BILLBOARD_SCALING_WORLD_SIZE' | 'BILLBOARD_SCALING_SCREEN_SIZE' | 'BILLBOARD_SCALING_CONST_SCREEN_SIZE' | number;
        /** Returns the size of the marker. */
        'size': number;
        /** Returns the vertical offset of the billboard. */
        'verticalOffset': number;
    };
    'massif::MassifApi': {
    };
    'massif::MassifInterop': {
    };
    'massif::MemoryCacheTileDataSource': {
        'capacity': number;
        /** (read-only) Returns the extent of the tiles in this data source. The bounds are in coordinate system of the projection of the data source. */
        readonly 'dataExtent': Bounds;
        /** (read-only) Returns the original data source that the cache uses. */
        readonly 'dataSource': Handle;
        /** (read-only) Returns the extent of the tiles in this data source. The bounds are in coordinate system of the projection of the data source. */
        readonly 'dataSource.dataExtent': Bounds;
        /** Gets the current maximum overzoom level for this datasource. Over it the datasource will not be "drawn" */
        'dataSource.maxOverzoomLevel': number;
        /** (read-only) Returns the maximum zoom level supported by this data source. */
        readonly 'dataSource.maxZoom': number;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        'dataSource.metaData': Record<string, Json>;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        [key: `dataSource.metaData.${string}`]: Json;
        /** (read-only) Returns the minimum zoom level supported by this data source. */
        readonly 'dataSource.minZoom': number;
        /** (read-only) Returns the projection of this tile source. */
        readonly 'dataSource.projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'dataSource.projection.bounds': Bounds;
        readonly 'dataSource.projection.name': string;
        /** Gets the current maximum overzoom level for this datasource. Over it the datasource will not be "drawn" */
        'maxOverzoomLevel': number;
        /** (read-only) Returns the maximum zoom level supported by this data source. */
        readonly 'maxZoom': number;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        'metaData': Record<string, Json>;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        [key: `metaData.${string}`]: Json;
        /** (read-only) Returns the minimum zoom level supported by this data source. */
        readonly 'minZoom': number;
        /** (read-only) Returns the projection of this tile source. */
        readonly 'projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'projection.bounds': Bounds;
        readonly 'projection.name': string;
    };
    'massif::MergedMBVTTileDataSource': {
        /** (read-only) Returns the extent of the tiles in this data source. The bounds are in coordinate system of the projection of the data source. */
        readonly 'dataExtent': Bounds;
        /** Gets the current maximum overzoom level for this datasource. Over it the datasource will not be "drawn" */
        'maxOverzoomLevel': number;
        /** (read-only) Returns the maximum zoom level supported by this data source. */
        readonly 'maxZoom': number;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        'metaData': Record<string, Json>;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        [key: `metaData.${string}`]: Json;
        /** (read-only) Returns the minimum zoom level supported by this data source. */
        readonly 'minZoom': number;
        /** (read-only) Returns the projection of this tile source. */
        readonly 'projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'projection.bounds': Bounds;
        readonly 'projection.name': string;
    };
    'massif::MultiGeometry': {
        /** (read-only) Returns the minimal bounds for the geometry. */
        readonly 'bounds': Bounds;
        readonly 'centerPos': Position;
        /** (read-only) Returns the geometry as a GeoJSON string, in its own coordinates. Here rather than only on Feature because serialising a shape otherwise means constructing a GeoJSONGeometryWriter, which no string-based binding can do. */
        readonly 'geoJSON': string;
        /** (read-only) Returns the number of geometry objects in this multi geometry container. */
        readonly 'geometryCount': number;
        readonly 'type': 'GEOMETRY_TYPE_POINT' | 'GEOMETRY_TYPE_LINE' | 'GEOMETRY_TYPE_POLYGON' | 'GEOMETRY_TYPE_MULTIPOINT' | 'GEOMETRY_TYPE_MULTILINE' | 'GEOMETRY_TYPE_MULTIPOLYGON' | 'GEOMETRY_TYPE_COLLECTION' | number;
    };
    'massif::MultiLineGeometry': {
        /** (read-only) Returns the minimal bounds for the geometry. */
        readonly 'bounds': Bounds;
        readonly 'centerPos': Position;
        /** (read-only) Returns the geometry as a GeoJSON string, in its own coordinates. Here rather than only on Feature because serialising a shape otherwise means constructing a GeoJSONGeometryWriter, which no string-based binding can do. */
        readonly 'geoJSON': string;
        /** (read-only) Returns the number of geometry objects in this multi geometry container. */
        readonly 'geometryCount': number;
        readonly 'type': 'GEOMETRY_TYPE_POINT' | 'GEOMETRY_TYPE_LINE' | 'GEOMETRY_TYPE_POLYGON' | 'GEOMETRY_TYPE_MULTIPOINT' | 'GEOMETRY_TYPE_MULTILINE' | 'GEOMETRY_TYPE_MULTIPOLYGON' | 'GEOMETRY_TYPE_COLLECTION' | number;
    };
    'massif::MultiOSMOfflineGeocodingService': {
        'autocomplete': boolean;
        'language': string;
        'maxResults': number;
    };
    'massif::MultiOSMOfflineReverseGeocodingService': {
        'language': string;
    };
    'massif::MultiPointGeometry': {
        /** (read-only) Returns the minimal bounds for the geometry. */
        readonly 'bounds': Bounds;
        readonly 'centerPos': Position;
        /** (read-only) Returns the geometry as a GeoJSON string, in its own coordinates. Here rather than only on Feature because serialising a shape otherwise means constructing a GeoJSONGeometryWriter, which no string-based binding can do. */
        readonly 'geoJSON': string;
        /** (read-only) Returns the number of geometry objects in this multi geometry container. */
        readonly 'geometryCount': number;
        readonly 'type': 'GEOMETRY_TYPE_POINT' | 'GEOMETRY_TYPE_LINE' | 'GEOMETRY_TYPE_POLYGON' | 'GEOMETRY_TYPE_MULTIPOINT' | 'GEOMETRY_TYPE_MULTILINE' | 'GEOMETRY_TYPE_MULTIPOLYGON' | 'GEOMETRY_TYPE_COLLECTION' | number;
    };
    'massif::MultiPolygonGeometry': {
        /** (read-only) Returns the minimal bounds for the geometry. */
        readonly 'bounds': Bounds;
        readonly 'centerPos': Position;
        /** (read-only) Returns the geometry as a GeoJSON string, in its own coordinates. Here rather than only on Feature because serialising a shape otherwise means constructing a GeoJSONGeometryWriter, which no string-based binding can do. */
        readonly 'geoJSON': string;
        /** (read-only) Returns the number of geometry objects in this multi geometry container. */
        readonly 'geometryCount': number;
        readonly 'type': 'GEOMETRY_TYPE_POINT' | 'GEOMETRY_TYPE_LINE' | 'GEOMETRY_TYPE_POLYGON' | 'GEOMETRY_TYPE_MULTIPOINT' | 'GEOMETRY_TYPE_MULTILINE' | 'GEOMETRY_TYPE_MULTIPOLYGON' | 'GEOMETRY_TYPE_COLLECTION' | number;
    };
    'massif::MultiTileDataSource': {
        /** (read-only) Returns the extent of the tiles in this data source. The bounds are in coordinate system of the projection of the data source. */
        readonly 'dataExtent': Bounds;
        /** Gets the current maximum overzoom level for this datasource. Over it the datasource will not be "drawn" */
        'maxOverzoomLevel': number;
        /** (read-only) Returns the maximum zoom level supported by this data source. */
        readonly 'maxZoom': number;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        'metaData': Record<string, Json>;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        [key: `metaData.${string}`]: Json;
        /** (read-only) Returns the minimum zoom level supported by this data source. */
        readonly 'minZoom': number;
        /** (read-only) Returns the projection of this tile source. */
        readonly 'projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'projection.bounds': Bounds;
        readonly 'projection.name': string;
    };
    'massif::MultiValhallaOfflineRoutingService': {
        'profile': string;
    };
    'massif::NMLModel': {
        /** Returns the base billboard this billboard is attached to. */
        'baseBillboard': Handle;
        /** Returns the base billboard this billboard is attached to. */
        'baseBillboard.baseBillboard': Handle;
        /** (read-only) Returns the bounds of this billboard or the base billboard, if there is one. */
        readonly 'baseBillboard.bounds': Bounds;
        /** Returns the geometry object that defines the location of this billboard. */
        'baseBillboard.geometry': Handle;
        /** (read-only) Returns the minimal bounds for the geometry. */
        readonly 'baseBillboard.geometry.bounds': Bounds;
        readonly 'baseBillboard.geometry.centerPos': Position;
        /** (read-only) Returns the geometry as a GeoJSON string, in its own coordinates. Here rather than only on Feature because serialising a shape otherwise means constructing a GeoJSONGeometryWriter, which no string-based binding can do. */
        readonly 'baseBillboard.geometry.geoJSON': string;
        readonly 'baseBillboard.geometry.type': 'GEOMETRY_TYPE_POINT' | 'GEOMETRY_TYPE_LINE' | 'GEOMETRY_TYPE_POLYGON' | 'GEOMETRY_TYPE_MULTIPOINT' | 'GEOMETRY_TYPE_MULTILINE' | 'GEOMETRY_TYPE_MULTIPOLYGON' | 'GEOMETRY_TYPE_COLLECTION' | number;
        /** Returns the internal id of this vector element. */
        'baseBillboard.id': number;
        /** Returns a copy of the vector element meta data map. The changes you make to this map are NOT reflected in the actual meta data of the element. */
        'baseBillboard.metaData': Record<string, Json>;
        /** Returns a copy of the vector element meta data map. The changes you make to this map are NOT reflected in the actual meta data of the element. */
        [key: `baseBillboard.metaData.${string}`]: Json;
        /** (read-only) Returns the location of the root billboard: getGeometry() if this billboard has a location, otherwise the location found by following the chain of base billboards to its root. */
        readonly 'baseBillboard.rootGeometry': Handle;
        /** (read-only) Returns the minimal bounds for the geometry. */
        readonly 'baseBillboard.rootGeometry.bounds': Bounds;
        readonly 'baseBillboard.rootGeometry.centerPos': Position;
        /** (read-only) Returns the geometry as a GeoJSON string, in its own coordinates. Here rather than only on Feature because serialising a shape otherwise means constructing a GeoJSONGeometryWriter, which no string-based binding can do. */
        readonly 'baseBillboard.rootGeometry.geoJSON': string;
        readonly 'baseBillboard.rootGeometry.type': 'GEOMETRY_TYPE_POINT' | 'GEOMETRY_TYPE_LINE' | 'GEOMETRY_TYPE_POLYGON' | 'GEOMETRY_TYPE_MULTIPOINT' | 'GEOMETRY_TYPE_MULTILINE' | 'GEOMETRY_TYPE_MULTIPOLYGON' | 'GEOMETRY_TYPE_COLLECTION' | number;
        /** Returns the rotation angle of this billboard. */
        'baseBillboard.rotation': number;
        /** Returns the state of the visibility flag of this vector element. */
        'baseBillboard.visible': boolean;
        /** (read-only) Returns the bounds of this billboard or the base billboard, if there is one. */
        readonly 'bounds': Bounds;
        /** Returns the geometry object that defines the location of this billboard. */
        'geometry': Handle;
        /** (read-only) Returns the minimal bounds for the geometry. */
        readonly 'geometry.bounds': Bounds;
        readonly 'geometry.centerPos': Position;
        /** (read-only) Returns the geometry as a GeoJSON string, in its own coordinates. Here rather than only on Feature because serialising a shape otherwise means constructing a GeoJSONGeometryWriter, which no string-based binding can do. */
        readonly 'geometry.geoJSON': string;
        readonly 'geometry.type': 'GEOMETRY_TYPE_POINT' | 'GEOMETRY_TYPE_LINE' | 'GEOMETRY_TYPE_POLYGON' | 'GEOMETRY_TYPE_MULTIPOINT' | 'GEOMETRY_TYPE_MULTILINE' | 'GEOMETRY_TYPE_MULTIPOLYGON' | 'GEOMETRY_TYPE_COLLECTION' | number;
        /** Returns the internal id of this vector element. */
        'id': number;
        /** Returns a copy of the vector element meta data map. The changes you make to this map are NOT reflected in the actual meta data of the element. */
        'metaData': Record<string, Json>;
        /** Returns a copy of the vector element meta data map. The changes you make to this map are NOT reflected in the actual meta data of the element. */
        [key: `metaData.${string}`]: Json;
        /** (read-only) Returns the location of the root billboard: getGeometry() if this billboard has a location, otherwise the location found by following the chain of base billboards to its root. */
        readonly 'rootGeometry': Handle;
        /** (read-only) Returns the minimal bounds for the geometry. */
        readonly 'rootGeometry.bounds': Bounds;
        readonly 'rootGeometry.centerPos': Position;
        /** (read-only) Returns the geometry as a GeoJSON string, in its own coordinates. Here rather than only on Feature because serialising a shape otherwise means constructing a GeoJSONGeometryWriter, which no string-based binding can do. */
        readonly 'rootGeometry.geoJSON': string;
        readonly 'rootGeometry.type': 'GEOMETRY_TYPE_POINT' | 'GEOMETRY_TYPE_LINE' | 'GEOMETRY_TYPE_POLYGON' | 'GEOMETRY_TYPE_MULTIPOINT' | 'GEOMETRY_TYPE_MULTILINE' | 'GEOMETRY_TYPE_MULTIPOLYGON' | 'GEOMETRY_TYPE_COLLECTION' | number;
        /** Returns the rotation angle of this billboard. */
        'rotation': number;
        /** Returns the rotation angle of this model. This is deprecated. Use getRotation instead. */
        'rotationAngle': number;
        /** Returns the rotation axis of this model. If rotation angle is 0, then the axis is irrelevant. */
        'rotationAxis': Position;
        /** Returns the scale of this model. */
        'scale': number;
        /** Returns the style of this object. */
        'style': Handle;
        /** (read-only) Returns the animation style of the billboard. */
        readonly 'style.animationStyle': Handle;
        /** (read-only) Returns the fade animation type. */
        readonly 'style.animationStyle.fadeAnimationType': 'ANIMATION_TYPE_NONE' | 'ANIMATION_TYPE_STEP' | 'ANIMATION_TYPE_LINEAR' | 'ANIMATION_TYPE_SMOOTHSTEP' | 'ANIMATION_TYPE_SPRING' | number;
        /** (read-only) Returns the phase-in duration of the animation. */
        readonly 'style.animationStyle.phaseInDuration': number;
        /** (read-only) Returns the phase-out duration of the animation. */
        readonly 'style.animationStyle.phaseOutDuration': number;
        /** (read-only) Returns the relative speed of the animation. */
        readonly 'style.animationStyle.relativeSpeed': number;
        /** (read-only) Returns the size-related animation type. */
        readonly 'style.animationStyle.sizeAnimationType': 'ANIMATION_TYPE_NONE' | 'ANIMATION_TYPE_STEP' | 'ANIMATION_TYPE_LINEAR' | 'ANIMATION_TYPE_SMOOTHSTEP' | 'ANIMATION_TYPE_SPRING' | number;
        /** (read-only) Returns the horizontal attaching anchor point of the billoard. */
        readonly 'style.attachAnchorPointX': number;
        /** (read-only) Returns the vertical attaching anchor point of the billoard. */
        readonly 'style.attachAnchorPointY': number;
        /** (read-only) Returns the state of the causes overlap flag. */
        readonly 'style.causesOverlap': boolean;
        /** (read-only) Returns the color of the vector element. */
        readonly 'style.color': number;
        /** (read-only) Returns the state of the allow overlap flag. */
        readonly 'style.hideIfOverlapped': boolean;
        /** (read-only) Returns the horizontal offset of the billboard. */
        readonly 'style.horizontalOffset': number;
        /** (read-only) Returns the model asset of the object. */
        readonly 'style.modelAsset': Handle;
        /** (read-only) Returns the size of the data */
        readonly 'style.modelAsset.size': number;
        /** (read-only) Returns the orientation mode of the model. */
        readonly 'style.orientationMode': 'BILLBOARD_ORIENTATION_FACE_CAMERA' | 'BILLBOARD_ORIENTATION_FACE_CAMERA_GROUND' | 'BILLBOARD_ORIENTATION_GROUND' | number;
        /** (read-only) Returns the placement priority of the billboard. */
        readonly 'style.placementPriority': number;
        /** (read-only) Returns the state of the scale with DPI flag. */
        readonly 'style.scaleWithDPI': boolean;
        /** (read-only) Returns the scaling mode of the model. */
        readonly 'style.scalingMode': 'BILLBOARD_SCALING_WORLD_SIZE' | 'BILLBOARD_SCALING_SCREEN_SIZE' | 'BILLBOARD_SCALING_CONST_SCREEN_SIZE' | number;
        /** (read-only) Returns the vertical offset of the billboard. */
        readonly 'style.verticalOffset': number;
        /** Returns the state of the visibility flag of this vector element. */
        'visible': boolean;
    };
    'massif::NMLModelStyle': {
        /** (read-only) Returns the animation style of the billboard. */
        readonly 'animationStyle': Handle;
        /** (read-only) Returns the fade animation type. */
        readonly 'animationStyle.fadeAnimationType': 'ANIMATION_TYPE_NONE' | 'ANIMATION_TYPE_STEP' | 'ANIMATION_TYPE_LINEAR' | 'ANIMATION_TYPE_SMOOTHSTEP' | 'ANIMATION_TYPE_SPRING' | number;
        /** (read-only) Returns the phase-in duration of the animation. */
        readonly 'animationStyle.phaseInDuration': number;
        /** (read-only) Returns the phase-out duration of the animation. */
        readonly 'animationStyle.phaseOutDuration': number;
        /** (read-only) Returns the relative speed of the animation. */
        readonly 'animationStyle.relativeSpeed': number;
        /** (read-only) Returns the size-related animation type. */
        readonly 'animationStyle.sizeAnimationType': 'ANIMATION_TYPE_NONE' | 'ANIMATION_TYPE_STEP' | 'ANIMATION_TYPE_LINEAR' | 'ANIMATION_TYPE_SMOOTHSTEP' | 'ANIMATION_TYPE_SPRING' | number;
        /** (read-only) Returns the horizontal attaching anchor point of the billoard. */
        readonly 'attachAnchorPointX': number;
        /** (read-only) Returns the vertical attaching anchor point of the billoard. */
        readonly 'attachAnchorPointY': number;
        /** (read-only) Returns the state of the causes overlap flag. */
        readonly 'causesOverlap': boolean;
        /** (read-only) Returns the color of the vector element. */
        readonly 'color': number;
        /** (read-only) Returns the state of the allow overlap flag. */
        readonly 'hideIfOverlapped': boolean;
        /** (read-only) Returns the horizontal offset of the billboard. */
        readonly 'horizontalOffset': number;
        /** (read-only) Returns the model asset of the object. */
        readonly 'modelAsset': Handle;
        /** (read-only) Returns the size of the data */
        readonly 'modelAsset.size': number;
        /** (read-only) Returns the orientation mode of the model. */
        readonly 'orientationMode': 'BILLBOARD_ORIENTATION_FACE_CAMERA' | 'BILLBOARD_ORIENTATION_FACE_CAMERA_GROUND' | 'BILLBOARD_ORIENTATION_GROUND' | number;
        /** (read-only) Returns the placement priority of the billboard. */
        readonly 'placementPriority': number;
        /** (read-only) Returns the state of the scale with DPI flag. */
        readonly 'scaleWithDPI': boolean;
        /** (read-only) Returns the scaling mode of the model. */
        readonly 'scalingMode': 'BILLBOARD_SCALING_WORLD_SIZE' | 'BILLBOARD_SCALING_SCREEN_SIZE' | 'BILLBOARD_SCALING_CONST_SCREEN_SIZE' | number;
        /** (read-only) Returns the vertical offset of the billboard. */
        readonly 'verticalOffset': number;
    };
    'massif::NMLModelStyleBuilder': {
        /** Returns the animation style of the billboard. */
        'animationStyle': Handle;
        /** (read-only) Returns the fade animation type. */
        readonly 'animationStyle.fadeAnimationType': 'ANIMATION_TYPE_NONE' | 'ANIMATION_TYPE_STEP' | 'ANIMATION_TYPE_LINEAR' | 'ANIMATION_TYPE_SMOOTHSTEP' | 'ANIMATION_TYPE_SPRING' | number;
        /** (read-only) Returns the phase-in duration of the animation. */
        readonly 'animationStyle.phaseInDuration': number;
        /** (read-only) Returns the phase-out duration of the animation. */
        readonly 'animationStyle.phaseOutDuration': number;
        /** (read-only) Returns the relative speed of the animation. */
        readonly 'animationStyle.relativeSpeed': number;
        /** (read-only) Returns the size-related animation type. */
        readonly 'animationStyle.sizeAnimationType': 'ANIMATION_TYPE_NONE' | 'ANIMATION_TYPE_STEP' | 'ANIMATION_TYPE_LINEAR' | 'ANIMATION_TYPE_SMOOTHSTEP' | 'ANIMATION_TYPE_SPRING' | number;
        /** Returns the horizontal attaching anchor point of the billboard. */
        'attachAnchorPointX': number;
        /** Returns the vertical attaching anchor point of the billboard. */
        'attachAnchorPointY': number;
        /** Returns the state of the causes overlap flag. */
        'causesOverlap': boolean;
        /** Returns the color of the vector element. */
        'color': number;
        /** Returns the state of the allow overlap flag. */
        'hideIfOverlapped': boolean;
        /** Returns the horizontal offset of the billboard. */
        'horizontalOffset': number;
        /** Returns the model asset of the object. */
        'modelAsset': Handle;
        /** (read-only) Returns the size of the data */
        readonly 'modelAsset.size': number;
        /** Returns the orientation mode of the model. */
        'orientationMode': 'BILLBOARD_ORIENTATION_FACE_CAMERA' | 'BILLBOARD_ORIENTATION_FACE_CAMERA_GROUND' | 'BILLBOARD_ORIENTATION_GROUND' | number;
        /** Returns the placement priority of the billboard. */
        'placementPriority': number;
        /** Returns the state of the scale with DPI flag. */
        'scaleWithDPI': boolean;
        /** Returns the scaling mode of the model. */
        'scalingMode': 'BILLBOARD_SCALING_WORLD_SIZE' | 'BILLBOARD_SCALING_SCREEN_SIZE' | 'BILLBOARD_SCALING_CONST_SCREEN_SIZE' | number;
        /** Returns the vertical offset of the billboard. */
        'verticalOffset': number;
    };
    'massif::OSMOfflineGeocodingService': {
        'autocomplete': boolean;
        'language': string;
        'maxResults': number;
    };
    'massif::OSMOfflineReverseGeocodingService': {
        'language': string;
    };
    'massif::OSRMOfflineRoutingService': {
        'profile': string;
    };
    'massif::OnChangeListener': {
    };
    'massif::Options': {
        /** Returns the dots per inch value. */
        'DPI': number;
        /** Returns the color of the ambient light. */
        'ambientLightColor': number;
        /** Returns the background bitmap. May be null. */
        'background': Handle;
        /** (read-only) Returns the bytes per pixel parameter of this bitmap. Valid values are 1, 2, 3 and 4. */
        readonly 'background.bytesPerPixel': number;
        /** (read-only) Returns the color format of this bitmap. */
        readonly 'background.colorFormat': 'COLOR_FORMAT_UNSUPPORTED' | 'COLOR_FORMAT_BGRA' | 'COLOR_FORMAT_RGBA_4444' | 'COLOR_FORMAT_RGB_565' | number;
        /** (read-only) Returns the height of the bitmap. */
        readonly 'background.height': number;
        /** (read-only) Returns the width of the bitmap. */
        readonly 'background.width': number;
        /** Returns the background bitmap. May be null. */
        'backgroundBitmap': Handle;
        /** (read-only) Returns the bytes per pixel parameter of this bitmap. Valid values are 1, 2, 3 and 4. */
        readonly 'backgroundBitmap.bytesPerPixel': number;
        /** (read-only) Returns the color format of this bitmap. */
        readonly 'backgroundBitmap.colorFormat': 'COLOR_FORMAT_UNSUPPORTED' | 'COLOR_FORMAT_BGRA' | 'COLOR_FORMAT_RGBA_4444' | 'COLOR_FORMAT_RGB_565' | number;
        /** (read-only) Returns the height of the bitmap. */
        readonly 'backgroundBitmap.height': number;
        /** (read-only) Returns the width of the bitmap. */
        readonly 'backgroundBitmap.width': number;
        /** Returns the base projection. */
        'baseProjection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'baseProjection.bounds': Bounds;
        readonly 'baseProjection.name': string;
        /** Returns the clear color used by the renderer before drawing anything else. By default, this is white. It should be set to (0, 0, 0, 0) if transparent MapView is needed. */
        'clearColor': number;
        /** Returns how far a pointer may travel before a press stops counting as a click. */
        'clickMovingTolerance': number;
        /** Returns the click type detection state. */
        'clickTypeDetection': boolean;
        /** Returns the state of the tile border debug overlay. */
        'debugTileBorders': boolean;
        /** Returns the double click detection state. */
        'doubleClickDetection': boolean;
        /** Returns the double click max duration in seconds. */
        'doubleClickMaxDuration': number;
        /** Returns the draw distance value. */
        'drawDistance': number;
        /** Returns the number of threads used by the envelope task pool. */
        'envelopeThreadPoolSize': number;
        /** Returns the vertical field of view angle. */
        'fieldOfViewY': number;
        /** Returns the focus point offset (from screen center) in pixels. */
        'focusPointOffset': [number, number];
        /** Returns the fog (atmosphere) options. May be null. */
        'fog': Handle;
        /** Returns the fog color. */
        'fog.color': number;
        /** Returns whether the fog is drawn at all. */
        'fog.enabled': boolean;
        /** Returns the color of the upper atmosphere. */
        'fog.highColor': number;
        /** Returns how far up the sky the fog is blended in. */
        'fog.horizonBlend': number;
        /** Returns where the fog reaches full strength. */
        'fog.rangeEnd': number;
        /** Returns where the fog starts. */
        'fog.rangeStart': number;
        /** Returns the custom fog fragment shader source, or an empty string if the built-in blend is used. */
        'fog.shaderSource': string;
        /** Returns the color of the sky at the zenith, beyond the atmosphere. */
        'fog.spaceColor': number;
        /** Returns how brightly stars are drawn beyond the atmosphere. */
        'fog.starIntensity': number;
        /** Returns the altitude the fog has fully faded out at. */
        'fog.verticalRangeEnd': number;
        /** Returns the altitude the fog starts fading out at. */
        'fog.verticalRangeStart': number;
        /** Returns the fog (atmosphere) options. May be null. */
        'fogOptions': Handle;
        /** Returns the fog color. */
        'fogOptions.color': number;
        /** Returns whether the fog is drawn at all. */
        'fogOptions.enabled': boolean;
        /** Returns the color of the upper atmosphere. */
        'fogOptions.highColor': number;
        /** Returns how far up the sky the fog is blended in. */
        'fogOptions.horizonBlend': number;
        /** Returns where the fog reaches full strength. */
        'fogOptions.rangeEnd': number;
        /** Returns where the fog starts. */
        'fogOptions.rangeStart': number;
        /** Returns the custom fog fragment shader source, or an empty string if the built-in blend is used. */
        'fogOptions.shaderSource': string;
        /** Returns the color of the sky at the zenith, beyond the atmosphere. */
        'fogOptions.spaceColor': number;
        /** Returns how brightly stars are drawn beyond the atmosphere. */
        'fogOptions.starIntensity': number;
        /** Returns the altitude the fog has fully faded out at. */
        'fogOptions.verticalRangeEnd': number;
        /** Returns the altitude the fog starts fading out at. */
        'fogOptions.verticalRangeStart': number;
        /** Returns how fast a FREE_ROAM_MODE_LOOK drag turns the view. */
        'freeRoamLookSensitivity': number;
        /** Returns the free roam mode. */
        'freeRoamMode': 'FREE_ROAM_MODE_OFF' | 'FREE_ROAM_MODE_LOOK' | 'FREE_ROAM_MODE_FIRST_PERSON' | number;
        /** Returns the first person move multiplier. */
        'freeRoamMoveSpeed': number;
        /** Returns the state of the kinetic panning flag. */
        'kineticPan': boolean;
        /** Returns the state of the kinetic rotation flag. */
        'kineticRotation': boolean;
        /** Returns the state of kinetic zoom flag. */
        'kineticZoom': boolean;
        /** Returns how far outside the viewport labels are placed, in screen pixels. */
        'labelPadding': number;
        /** Returns how far labels are placed, in multiples of the camera-to-focus distance. */
        'labelViewDistance': number;
        /** Returns wether layers are processed in reversed order to process labels. */
        'layersLabelsProcessedInReverseOrder': boolean;
        /** Returns the light (sun) options. May be null. */
        'light': Handle;
        /** Returns the ambient light color. */
        'light.ambientColor': number;
        /** Returns the ambient light intensity. */
        'light.ambientIntensity': number;
        /** Returns the day-cycle light curve. */
        'light.dayCycleLightStops': Json;
        /** Returns whether the sun's COLOURS follow its position. */
        'light.dayCycleLightsEnabled': boolean;
        /** Returns the curve used while the sun is RISING, if the app set one. */
        'light.dayCycleRisingLightStops': Json;
        /** Returns the shadow depth bias scale. */
        'light.shadowBias': number;
        /** Returns the number of shadow cascades. */
        'light.shadowCascades': number;
        /** Returns the shadow caster margin in tiles. */
        'light.shadowCasterMargin': number;
        /** Returns the shadow distance. */
        'light.shadowDistance': number;
        /** Returns the shadow map resolution. */
        'light.shadowMapSize': number;
        /** Returns the shadow normal offset. */
        'light.shadowNormalOffset': number;
        /** Returns the shadow softness. */
        'light.shadowSoftness': number;
        /** Returns the shadow strength. */
        'light.shadowStrength': number;
        /** Returns the sun altitude in degrees above the horizon. */
        'light.sunAltitude': number;
        /** Returns the sun azimuth in degrees. */
        'light.sunAzimuth': number;
        /** Returns the sun (directional light) color. */
        'light.sunColor': number;
        /** Returns the sun light intensity. */
        'light.sunIntensity': number;
        /** Returns whether this sun overrides the one a style states. */
        'light.sunOverridingStyle': boolean;
        /** Returns whether the sun lights the 3D terrain surface. */
        'light.terrainLightingEnabled': boolean;
        /** Returns the light (sun) options. May be null. */
        'lightOptions': Handle;
        /** Returns the ambient light color. */
        'lightOptions.ambientColor': number;
        /** Returns the ambient light intensity. */
        'lightOptions.ambientIntensity': number;
        /** Returns the day-cycle light curve. */
        'lightOptions.dayCycleLightStops': Json;
        /** Returns whether the sun's COLOURS follow its position. */
        'lightOptions.dayCycleLightsEnabled': boolean;
        /** Returns the curve used while the sun is RISING, if the app set one. */
        'lightOptions.dayCycleRisingLightStops': Json;
        /** Returns the shadow depth bias scale. */
        'lightOptions.shadowBias': number;
        /** Returns the number of shadow cascades. */
        'lightOptions.shadowCascades': number;
        /** Returns the shadow caster margin in tiles. */
        'lightOptions.shadowCasterMargin': number;
        /** Returns the shadow distance. */
        'lightOptions.shadowDistance': number;
        /** Returns the shadow map resolution. */
        'lightOptions.shadowMapSize': number;
        /** Returns the shadow normal offset. */
        'lightOptions.shadowNormalOffset': number;
        /** Returns the shadow softness. */
        'lightOptions.shadowSoftness': number;
        /** Returns the shadow strength. */
        'lightOptions.shadowStrength': number;
        /** Returns the sun altitude in degrees above the horizon. */
        'lightOptions.sunAltitude': number;
        /** Returns the sun azimuth in degrees. */
        'lightOptions.sunAzimuth': number;
        /** Returns the sun (directional light) color. */
        'lightOptions.sunColor': number;
        /** Returns the sun light intensity. */
        'lightOptions.sunIntensity': number;
        /** Returns whether this sun overrides the one a style states. */
        'lightOptions.sunOverridingStyle': boolean;
        /** Returns whether the sun lights the 3D terrain surface. */
        'lightOptions.terrainLightingEnabled': boolean;
        /** Returns the long click duration in seconds. */
        'longClickDuration': number;
        /** Returns the color of the main light. */
        'mainLightColor': number;
        /** Returns the direction of the main light. */
        'mainLightDirection': Position;
        /** Returns the map panning bounds constraints. Map bounds minimum and maximum points are in the base projection's coordinate system. */
        'panBounds': Bounds;
        /** Returns the panning mode. */
        'panningMode': 'PANNING_MODE_FREE' | 'PANNING_MODE_STICKY' | 'PANNING_MODE_STICKY_FINAL' | number;
        /** Returns the panning speed mode. */
        'panningSpeedMode': 'PANNING_SPEED_MODE_MAP' | 'PANNING_SPEED_MODE_ANCHORED' | 'PANNING_SPEED_MODE_CONSTANT' | number;
        /** Returns the pivot mode. */
        'pivotMode': 'PIVOT_MODE_TOUCHPOINT' | 'PIVOT_MODE_CENTERPOINT' | number;
        /** Returns the base projection. */
        'projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'projection.bounds': Bounds;
        readonly 'projection.name': string;
        /** Returns the render projection mode. */
        'renderProjectionMode': 'RENDER_PROJECTION_MODE_PLANAR' | 'RENDER_PROJECTION_MODE_SPHERICAL' | number;
        /** Returns the state of the restricted panning flag. */
        'restrictedPanning': boolean;
        /** Returns the state of the map rotatability flag. */
        'rotatable': boolean;
        /** Returns the state of rotation gestures. */
        'rotationGestures': boolean;
        /** Returns the state of seamless horizontal panning flag. */
        'seamlessPanning': boolean;
        /** Returns the sky options. May be null. */
        'sky': Handle;
        /** Returns the tint applied to Rayleigh scattering. */
        'sky.atmosphereColor': number;
        /** Returns the exposure applied to the scattered light. */
        'sky.atmosphereLuminance': number;
        /** Returns the brightness of the sun driving the atmosphere. */
        'sky.atmosphereSunIntensity': number;
        /** Returns whether the shader sky is enabled. */
        'sky.enabled': boolean;
        /** Returns the ground color. */
        'sky.groundColor': number;
        /** Returns the tint applied to Mie scattering. */
        'sky.haloColor': number;
        /** Returns the angular blend width between the horizon color and the sky color. */
        'sky.horizonBlend': number;
        /** Returns the horizon color. */
        'sky.horizonColor': number;
        /** Returns how finely the atmosphere is integrated. */
        'sky.quality': 'SKY_QUALITY_LOW' | 'SKY_QUALITY_MEDIUM' | 'SKY_QUALITY_HIGH' | number;
        /** Returns the custom sky fragment shader source, or an empty string if the built-in shader is used. */
        'sky.shaderSource': string;
        /** Returns the zenith sky color. */
        'sky.skyColor': number;
        /** Returns whether the built-in shader draws a sun disc. */
        'sky.sunDiscEnabled': boolean;
        /** Returns what the sky pass draws. */
        'sky.type': 'SKY_TYPE_GRADIENT' | 'SKY_TYPE_ATMOSPHERE' | number;
        /** Returns the sky color. */
        'skyColor': number;
        /** Returns the sky options. May be null. */
        'skyOptions': Handle;
        /** Returns the tint applied to Rayleigh scattering. */
        'skyOptions.atmosphereColor': number;
        /** Returns the exposure applied to the scattered light. */
        'skyOptions.atmosphereLuminance': number;
        /** Returns the brightness of the sun driving the atmosphere. */
        'skyOptions.atmosphereSunIntensity': number;
        /** Returns whether the shader sky is enabled. */
        'skyOptions.enabled': boolean;
        /** Returns the ground color. */
        'skyOptions.groundColor': number;
        /** Returns the tint applied to Mie scattering. */
        'skyOptions.haloColor': number;
        /** Returns the angular blend width between the horizon color and the sky color. */
        'skyOptions.horizonBlend': number;
        /** Returns the horizon color. */
        'skyOptions.horizonColor': number;
        /** Returns how finely the atmosphere is integrated. */
        'skyOptions.quality': 'SKY_QUALITY_LOW' | 'SKY_QUALITY_MEDIUM' | 'SKY_QUALITY_HIGH' | number;
        /** Returns the custom sky fragment shader source, or an empty string if the built-in shader is used. */
        'skyOptions.shaderSource': string;
        /** Returns the zenith sky color. */
        'skyOptions.skyColor': number;
        /** Returns whether the built-in shader draws a sun disc. */
        'skyOptions.sunDiscEnabled': boolean;
        /** Returns what the sky pass draws. */
        'skyOptions.type': 'SKY_TYPE_GRADIENT' | 'SKY_TYPE_ATMOSPHERE' | number;
        /** Returns the terrain options. May be null if no terrain is configured. */
        'terrain': Handle;
        /** Returns how long the terrain takes to sink flat. */
        'terrain.autoFlattenDuration': number;
        /** Returns the screen parallax below which the terrain renders flat. */
        'terrain.autoFlattenParallax': number;
        /** Returns how long the terrain takes to rise back into 3D. */
        'terrain.autoFlattenRiseDuration': number;
        /** Returns the tilt at or above which the terrain renders flat. */
        'terrain.autoFlattenTilt': number;
        /** Returns the terrain background color. */
        'terrain.backgroundColor': number;
        /** Returns the billboard/label terrain occlusion state. */
        'terrain.billboardOcclusionEnabled': boolean;
        /** Returns the billboard/label terrain occlusion tolerance. */
        'terrain.billboardOcclusionTolerance': number;
        /** Returns whether bridges and tunnels stand on their own chord (3D bridges). */
        'terrain.bridges3DEnabled': boolean;
        /** Returns the duration of the camera terrain-following correction animation. */
        'terrain.cameraClampDuration': number;
        /** Returns the camera terrain clearance floor: an explicit minimum height the camera is kept above the terrain surface, in meters. */
        'terrain.cameraClearance': number;
        /** Returns the share of the camera's altitude that the terrain clearance takes. */
        'terrain.cameraClearanceFraction': number;
        /** Returns the clip-space depth bias used when depth-testing draped 2D geometry against the terrain. */
        'terrain.depthBias': number;
        /** Returns the drape cache budget in megabytes. */
        'terrain.drapeCacheSize': number;
        /** Returns whether polygon fills are draped as a render-to-texture surface. */
        'terrain.drapeFillsEnabled': boolean;
        /** Returns whether vt tile lines are also draped (in addition to fills). */
        'terrain.drapeLinesEnabled': boolean;
        /** Returns the per-tile drape texture resolution, 0 when it follows the screen. */
        'terrain.drapeResolution': number;
        /** Returns how many drape tiles the automatic resolution assumes are cached at once. */
        'terrain.drapeWorkingSet': number;
        /** Returns the elevation grid cache budget in megabytes, 0 for the SDK's own rule. */
        'terrain.elevationCacheSize': number;
        /** Returns whether elevation tile prefetching is enabled. */
        'terrain.elevationPrefetchEnabled': boolean;
        /** Returns the enabled state of the terrain. */
        'terrain.enabled': boolean;
        /** Returns the terrain height exaggeration factor. */
        'terrain.exaggeration': number;
        /** Returns how far a flattened terrain goes back towards a plain 2D map. */
        'terrain.flattenMode': 'TERRAIN_FLATTEN_MODE_RENDER' | 'TERRAIN_FLATTEN_MODE_FULL' | number;
        /** Returns how far the terrain is flattened right now, 0 (full 3D) to 1 (flat). */
        'terrain.flattenRatio': number;
        /** Returns whether the map is asked to render flat. This is the 2D/3D state, whether it was set by the app or by auto-flattening; the switch itself is animated, so for a moment after a change the map is still on its way there. */
        'terrain.flattened': boolean;
        /** Returns the height the viewpoint is lifted above the ground-following focus, in meters. */
        'terrain.focusLift': number;
        /** Returns how many zoom levels below the camera a tile may coarsen to. */
        'terrain.maxTileZoomCoarsening': number;
        /** Returns the maximum visible tile zoom offset, relative to the camera zoom level. */
        'terrain.maxTileZoomOffset': number;
        /** Returns the maximum tile zoom level the terrain mesh is cut at. */
        'terrain.maxZoom': number;
        /** Returns how many terrain surface meshes may be cached. */
        'terrain.meshCacheSize': number;
        /** Returns the terrain mesh resolution. */
        'terrain.meshResolution': number;
        /** Returns the minimum tile zoom level with 3D terrain. */
        'terrain.minZoom': number;
        /** Returns the style layers that are kept out of the terrain drape bake. */
        'terrain.noDrapeLayerFilter': string;
        /** Returns the ground distance the surface normals are measured over, in meters. */
        'terrain.normalSampleDistance': number;
        /** Returns the downscale factor of the packed depth/normal texture post-process effects read. */
        'terrain.postProcessDownscale': number;
        /** Returns whether seamless tile edge handling is enabled. */
        'terrain.seamlessTileEdgesEnabled': boolean;
        /** Returns whether the shared ground pass draws the terrain a second time. */
        'terrain.sharedGroundEnabled': boolean;
        /** Returns the distance geo-three's terrain LOD subdivides at. */
        'terrain.subdivideDistance': number;
        /** Returns the resolution the elevation node field is built at. */
        'terrain.surfaceNodeResolution': number;
        /** Returns the custom terrain surface fragment shader source, or an empty string if no shaded surface is drawn. */
        'terrain.surfaceShaderSource': string;
        /** (read-only) Returns whether the switch is holding the ground flat while the tiles 3D needs load. */
        readonly 'terrain.switching': boolean;
        /** Returns the opacity a label keeps while its anchor is behind 3D content. */
        'terrain.textOcclusionOpacity': number;
        /** Returns whether cross-LOD tile edge stitching is enabled. */
        'terrain.tileEdgeStitchingEnabled': boolean;
        /** Returns the minimum view distance, in meters. */
        'terrain.viewDistance': number;
        /** Returns the factor applied to the view distance. */
        'terrain.viewDistanceFactor': number;
        /** Returns the maximum view distance, in meters. */
        'terrain.viewDistanceMax': number;
        /** Returns the terrain options. May be null if no terrain is configured. */
        'terrainOptions': Handle;
        /** Returns how long the terrain takes to sink flat. */
        'terrainOptions.autoFlattenDuration': number;
        /** Returns the screen parallax below which the terrain renders flat. */
        'terrainOptions.autoFlattenParallax': number;
        /** Returns how long the terrain takes to rise back into 3D. */
        'terrainOptions.autoFlattenRiseDuration': number;
        /** Returns the tilt at or above which the terrain renders flat. */
        'terrainOptions.autoFlattenTilt': number;
        /** Returns the terrain background color. */
        'terrainOptions.backgroundColor': number;
        /** Returns the billboard/label terrain occlusion state. */
        'terrainOptions.billboardOcclusionEnabled': boolean;
        /** Returns the billboard/label terrain occlusion tolerance. */
        'terrainOptions.billboardOcclusionTolerance': number;
        /** Returns whether bridges and tunnels stand on their own chord (3D bridges). */
        'terrainOptions.bridges3DEnabled': boolean;
        /** Returns the duration of the camera terrain-following correction animation. */
        'terrainOptions.cameraClampDuration': number;
        /** Returns the camera terrain clearance floor: an explicit minimum height the camera is kept above the terrain surface, in meters. */
        'terrainOptions.cameraClearance': number;
        /** Returns the share of the camera's altitude that the terrain clearance takes. */
        'terrainOptions.cameraClearanceFraction': number;
        /** Returns the clip-space depth bias used when depth-testing draped 2D geometry against the terrain. */
        'terrainOptions.depthBias': number;
        /** Returns the drape cache budget in megabytes. */
        'terrainOptions.drapeCacheSize': number;
        /** Returns whether polygon fills are draped as a render-to-texture surface. */
        'terrainOptions.drapeFillsEnabled': boolean;
        /** Returns whether vt tile lines are also draped (in addition to fills). */
        'terrainOptions.drapeLinesEnabled': boolean;
        /** Returns the per-tile drape texture resolution, 0 when it follows the screen. */
        'terrainOptions.drapeResolution': number;
        /** Returns how many drape tiles the automatic resolution assumes are cached at once. */
        'terrainOptions.drapeWorkingSet': number;
        /** Returns the elevation grid cache budget in megabytes, 0 for the SDK's own rule. */
        'terrainOptions.elevationCacheSize': number;
        /** Returns whether elevation tile prefetching is enabled. */
        'terrainOptions.elevationPrefetchEnabled': boolean;
        /** Returns the enabled state of the terrain. */
        'terrainOptions.enabled': boolean;
        /** Returns the terrain height exaggeration factor. */
        'terrainOptions.exaggeration': number;
        /** Returns how far a flattened terrain goes back towards a plain 2D map. */
        'terrainOptions.flattenMode': 'TERRAIN_FLATTEN_MODE_RENDER' | 'TERRAIN_FLATTEN_MODE_FULL' | number;
        /** Returns how far the terrain is flattened right now, 0 (full 3D) to 1 (flat). */
        'terrainOptions.flattenRatio': number;
        /** Returns whether the map is asked to render flat. This is the 2D/3D state, whether it was set by the app or by auto-flattening; the switch itself is animated, so for a moment after a change the map is still on its way there. */
        'terrainOptions.flattened': boolean;
        /** Returns the height the viewpoint is lifted above the ground-following focus, in meters. */
        'terrainOptions.focusLift': number;
        /** Returns how many zoom levels below the camera a tile may coarsen to. */
        'terrainOptions.maxTileZoomCoarsening': number;
        /** Returns the maximum visible tile zoom offset, relative to the camera zoom level. */
        'terrainOptions.maxTileZoomOffset': number;
        /** Returns the maximum tile zoom level the terrain mesh is cut at. */
        'terrainOptions.maxZoom': number;
        /** Returns how many terrain surface meshes may be cached. */
        'terrainOptions.meshCacheSize': number;
        /** Returns the terrain mesh resolution. */
        'terrainOptions.meshResolution': number;
        /** Returns the minimum tile zoom level with 3D terrain. */
        'terrainOptions.minZoom': number;
        /** Returns the style layers that are kept out of the terrain drape bake. */
        'terrainOptions.noDrapeLayerFilter': string;
        /** Returns the ground distance the surface normals are measured over, in meters. */
        'terrainOptions.normalSampleDistance': number;
        /** Returns the downscale factor of the packed depth/normal texture post-process effects read. */
        'terrainOptions.postProcessDownscale': number;
        /** Returns whether seamless tile edge handling is enabled. */
        'terrainOptions.seamlessTileEdgesEnabled': boolean;
        /** Returns whether the shared ground pass draws the terrain a second time. */
        'terrainOptions.sharedGroundEnabled': boolean;
        /** Returns the distance geo-three's terrain LOD subdivides at. */
        'terrainOptions.subdivideDistance': number;
        /** Returns the resolution the elevation node field is built at. */
        'terrainOptions.surfaceNodeResolution': number;
        /** Returns the custom terrain surface fragment shader source, or an empty string if no shaded surface is drawn. */
        'terrainOptions.surfaceShaderSource': string;
        /** (read-only) Returns whether the switch is holding the ground flat while the tiles 3D needs load. */
        readonly 'terrainOptions.switching': boolean;
        /** Returns the opacity a label keeps while its anchor is behind 3D content. */
        'terrainOptions.textOcclusionOpacity': number;
        /** Returns whether cross-LOD tile edge stitching is enabled. */
        'terrainOptions.tileEdgeStitchingEnabled': boolean;
        /** Returns the minimum view distance, in meters. */
        'terrainOptions.viewDistance': number;
        /** Returns the factor applied to the view distance. */
        'terrainOptions.viewDistanceFactor': number;
        /** Returns the maximum view distance, in meters. */
        'terrainOptions.viewDistanceMax': number;
        /** Returns the tile size used for drawing map tiles. */
        'tileDrawSize': number;
        /** Returns the factor on the screen size a tile may cover before it is refined. */
        'tileLODFactor': number;
        /** Returns how many distinct zoom levels a tilted view may spread over. */
        'tileLODMaxZoomLevelsOnScreen': number;
        /** Returns how many times more tiles a tilted view may load than a top-down one. */
        'tileLODTileCountRatio': number;
        /** Returns how many zoom levels above its own a coarsened tile may be styled at. */
        'tileStyleZoomLift': number;
        /** Returns the number of threads used by the tile task pool. */
        'tileThreadPoolSize': number;
        /** Returns true if tilting gesture direction is reversed (and same as with Google Maps). */
        'tiltGestureReversed': boolean;
        /** Returns the tilt range constraint. */
        'tiltRange': [number, number];
        /** Returns the state of the user input flag. */
        'userInput': boolean;
        /** Returns the state of zoom gestures. */
        'zoomGestures': boolean;
        /** Returns how many zoom levels the camera is offset from the tile-size convention. */
        'zoomOffset': number;
        /** Returns the zoom range constraint. */
        'zoomRange': [number, number];
    };
    'massif::OptionsListener': {
    };
    'massif::OrderedTileDataSource': {
        /** (read-only) Returns the extent of the tiles in this data source. The bounds are in coordinate system of the projection of the data source. */
        readonly 'dataExtent': Bounds;
        /** Gets the current maximum overzoom level for this datasource. Over it the datasource will not be "drawn" */
        'maxOverzoomLevel': number;
        /** (read-only) Returns the maximum zoom level supported by this data source. */
        readonly 'maxZoom': number;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        'metaData': Record<string, Json>;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        [key: `metaData.${string}`]: Json;
        /** (read-only) Returns the minimum zoom level supported by this data source. */
        readonly 'minZoom': number;
        /** (read-only) Returns the projection of this tile source. */
        readonly 'projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'projection.bounds': Bounds;
        readonly 'projection.name': string;
    };
    'massif::PMTilesTileDataSource': {
        /** (read-only) Returns the extent of the tiles in this data source. The bounds are in coordinate system of the projection of the data source. */
        readonly 'dataExtent': Bounds;
        /** Gets the current maximum overzoom level for this datasource. Over it the datasource will not be "drawn" */
        'maxOverzoomLevel': number;
        /** (read-only) Returns the maximum zoom level supported by this data source. */
        readonly 'maxZoom': number;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        'metaData': Record<string, Json>;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        [key: `metaData.${string}`]: Json;
        /** (read-only) Returns the minimum zoom level supported by this data source. */
        readonly 'minZoom': number;
        /** (read-only) Returns the projection of this tile source. */
        readonly 'projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'projection.bounds': Bounds;
        readonly 'projection.name': string;
    };
    'massif::PackageInfo': {
        /** (read-only) Returns package meta info. If package contains no meta info, null is returned. */
        readonly 'metaInfo': Handle;
        /** (read-only) Returns the underlying variant. */
        readonly 'metaInfo.variant': Json;
        /** (read-only) Returns the default name (short description) of the package. It is better to use getNames method instead, as each package may contain multiple names. The name returned is generic name or if that is not available, then English name. */
        readonly 'name': string;
        /** (read-only) Returns the internal package id. This should not be displayed to the user. */
        readonly 'packageId': string;
        /** (read-only) Returns the package type. */
        readonly 'packageType': 'PACKAGE_TYPE_MAP' | 'PACKAGE_TYPE_ROUTING' | 'PACKAGE_TYPE_GEOCODING' | 'PACKAGE_TYPE_VALHALLA_ROUTING' | number;
        /** (read-only) Returns the size of the package in bytes. This can be displayed to the user. */
        readonly 'size': number;
        /** (read-only) Returns the encoded tile mask of the package. This is available for map packages but not for routing packages. This should not be displayed to the user. */
        readonly 'tileMask': Handle;
        /** (read-only) Returns maximum zoom level encoded in this tilemask. */
        readonly 'tileMask.maxZoomLevel': number;
        /** (read-only) Returns the encoded tile mask value. This should not be displayed to the user. */
        readonly 'tileMask.stringValue': string;
        /** (read-only) Returns the package version. This should not be displayed to the user. */
        readonly 'version': number;
    };
    'massif::PackageManager': {
        readonly 'localPackages': Json;
        'packageManagerListener': Handle;
        readonly 'serverPackageListAge': number;
        readonly 'serverPackageListMetaInfo': Handle;
        /** (read-only) Returns the underlying variant. */
        readonly 'serverPackageListMetaInfo.variant': Json;
        readonly 'serverPackages': Json;
    };
    'massif::PackageManagerGeocodingService': {
        'autocomplete': boolean;
        'language': string;
        'maxResults': number;
    };
    'massif::PackageManagerListener': {
    };
    'massif::PackageManagerReverseGeocodingService': {
        'language': string;
    };
    'massif::PackageManagerRoutingService': {
        'profile': string;
    };
    'massif::PackageManagerTileDataSource': {
        /** (read-only) Returns the extent of the tiles in this data source. The bounds are in coordinate system of the projection of the data source. */
        readonly 'dataExtent': Bounds;
        /** Gets the current maximum overzoom level for this datasource. Over it the datasource will not be "drawn" */
        'maxOverzoomLevel': number;
        /** (read-only) Returns the maximum zoom level supported by this data source. */
        readonly 'maxZoom': number;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        'metaData': Record<string, Json>;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        [key: `metaData.${string}`]: Json;
        /** (read-only) Returns the minimum zoom level supported by this data source. */
        readonly 'minZoom': number;
        /** (read-only) Returns the package manager instance used by the data source. */
        readonly 'packageManager': Handle;
        readonly 'packageManager.localPackages': Json;
        'packageManager.packageManagerListener': Handle;
        readonly 'packageManager.serverPackageListAge': number;
        readonly 'packageManager.serverPackageListMetaInfo': Handle;
        /** (read-only) Returns the underlying variant. */
        readonly 'packageManager.serverPackageListMetaInfo.variant': Json;
        readonly 'packageManager.serverPackages': Json;
        /** (read-only) Returns the projection of this tile source. */
        readonly 'projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'projection.bounds': Bounds;
        readonly 'projection.name': string;
    };
    'massif::PackageManagerValhallaRoutingService': {
        'profile': string;
    };
    'massif::PackageMetaInfo': {
        /** (read-only) Returns the underlying variant. */
        readonly 'variant': Json;
    };
    'massif::PackageStatus': {
        /** (read-only) Returns the current action being performed. */
        readonly 'currentAction': 'PACKAGE_ACTION_READY' | 'PACKAGE_ACTION_WAITING' | 'PACKAGE_ACTION_DOWNLOADING' | 'PACKAGE_ACTION_COPYING' | 'PACKAGE_ACTION_REMOVING' | number;
        /** (read-only) Returns the paused state of the action. */
        readonly 'paused': boolean;
        /** (read-only) Returns the progress of the action. */
        readonly 'progress': number;
    };
    'massif::PackageTileMask': {
        /** (read-only) Returns maximum zoom level encoded in this tilemask. */
        readonly 'maxZoomLevel': number;
        /** (read-only) Returns the encoded tile mask value. This should not be displayed to the user. */
        readonly 'stringValue': string;
    };
    'massif::PeliasOnlineGeocodingService': {
        'autocomplete': boolean;
        /** Returns the custom backend service URL. */
        'customServiceURL': string;
        'language': string;
        'maxResults': number;
    };
    'massif::PeliasOnlineReverseGeocodingService': {
        /** Returns the custom backend service URL. */
        'customServiceURL': string;
        'language': string;
    };
    'massif::PersistentCacheTileDataSource': {
        /** Returns the state of cache only mode. */
        'cacheOnlyMode': boolean;
        'capacity': number;
        /** (read-only) Returns the extent of the tiles in this data source. The bounds are in coordinate system of the projection of the data source. */
        readonly 'dataExtent': Bounds;
        /** (read-only) Returns the original data source that the cache uses. */
        readonly 'dataSource': Handle;
        /** (read-only) Returns the extent of the tiles in this data source. The bounds are in coordinate system of the projection of the data source. */
        readonly 'dataSource.dataExtent': Bounds;
        /** Gets the current maximum overzoom level for this datasource. Over it the datasource will not be "drawn" */
        'dataSource.maxOverzoomLevel': number;
        /** (read-only) Returns the maximum zoom level supported by this data source. */
        readonly 'dataSource.maxZoom': number;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        'dataSource.metaData': Record<string, Json>;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        [key: `dataSource.metaData.${string}`]: Json;
        /** (read-only) Returns the minimum zoom level supported by this data source. */
        readonly 'dataSource.minZoom': number;
        /** (read-only) Returns the projection of this tile source. */
        readonly 'dataSource.projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'dataSource.projection.bounds': Bounds;
        readonly 'dataSource.projection.name': string;
        /** Gets the current maximum overzoom level for this datasource. Over it the datasource will not be "drawn" */
        'maxOverzoomLevel': number;
        /** (read-only) Returns the maximum zoom level supported by this data source. */
        readonly 'maxZoom': number;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        'metaData': Record<string, Json>;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        [key: `metaData.${string}`]: Json;
        /** (read-only) Returns the minimum zoom level supported by this data source. */
        readonly 'minZoom': number;
        /** (read-only) Returns the status of the cache database. */
        readonly 'open': boolean;
        /** (read-only) Returns the projection of this tile source. */
        readonly 'projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'projection.bounds': Bounds;
        readonly 'projection.name': string;
    };
    'massif::PersistentTaskQueue': {
    };
    'massif::Point': {
        /** (read-only) Returns the bounds of this vector element. */
        readonly 'bounds': Bounds;
        'geometry': Handle;
        /** (read-only) Returns the minimal bounds for the geometry. */
        readonly 'geometry.bounds': Bounds;
        readonly 'geometry.centerPos': Position;
        /** (read-only) Returns the geometry as a GeoJSON string, in its own coordinates. Here rather than only on Feature because serialising a shape otherwise means constructing a GeoJSONGeometryWriter, which no string-based binding can do. */
        readonly 'geometry.geoJSON': string;
        /** (read-only) Returns the position of the point. */
        readonly 'geometry.pos': Position;
        readonly 'geometry.type': 'GEOMETRY_TYPE_POINT' | 'GEOMETRY_TYPE_LINE' | 'GEOMETRY_TYPE_POLYGON' | 'GEOMETRY_TYPE_MULTIPOINT' | 'GEOMETRY_TYPE_MULTILINE' | 'GEOMETRY_TYPE_MULTIPOLYGON' | 'GEOMETRY_TYPE_COLLECTION' | number;
        /** Returns the internal id of this vector element. */
        'id': number;
        /** Returns a copy of the vector element meta data map. The changes you make to this map are NOT reflected in the actual meta data of the element. */
        'metaData': Record<string, Json>;
        /** Returns a copy of the vector element meta data map. The changes you make to this map are NOT reflected in the actual meta data of the element. */
        [key: `metaData.${string}`]: Json;
        /** Returns the style of this point. */
        'style': Handle;
        /** (read-only) Returns the bitmap of the point. */
        readonly 'style.bitmap': Handle;
        /** (read-only) Returns the bytes per pixel parameter of this bitmap. Valid values are 1, 2, 3 and 4. */
        readonly 'style.bitmap.bytesPerPixel': number;
        /** (read-only) Returns the color format of this bitmap. */
        readonly 'style.bitmap.colorFormat': 'COLOR_FORMAT_UNSUPPORTED' | 'COLOR_FORMAT_BGRA' | 'COLOR_FORMAT_RGBA_4444' | 'COLOR_FORMAT_RGB_565' | number;
        /** (read-only) Returns the height of the bitmap. */
        readonly 'style.bitmap.height': number;
        /** (read-only) Returns the width of the bitmap. */
        readonly 'style.bitmap.width': number;
        /** (read-only) Returns the size of the point used for click detection. */
        readonly 'style.clickSize': number;
        /** (read-only) Returns the color of the vector element. */
        readonly 'style.color': number;
        /** (read-only) Returns the size of the point. */
        readonly 'style.size': number;
        /** Returns the state of the visibility flag of this vector element. */
        'visible': boolean;
    };
    'massif::PointDetailTileDataSource': {
        /** (read-only) Returns the extent of the tiles in this data source. The bounds are in coordinate system of the projection of the data source. */
        readonly 'dataExtent': Bounds;
        /** Returns the zoom whose tiles are read. */
        'detailZoom': number;
        /** (read-only) Returns the layer that is rebuilt. */
        readonly 'layerName': string;
        /** Returns how many zoom levels below the requested tile this will reach. */
        'maxDetailLevels': number;
        /** Returns how many features a rebuilt tile may carry. */
        'maxFeatures': number;
        /** Gets the current maximum overzoom level for this datasource. Over it the datasource will not be "drawn" */
        'maxOverzoomLevel': number;
        /** (read-only) Returns the maximum zoom level supported by this data source. */
        readonly 'maxZoom': number;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        'metaData': Record<string, Json>;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        [key: `metaData.${string}`]: Json;
        /** (read-only) Returns the minimum zoom level supported by this data source. */
        readonly 'minZoom': number;
        /** (read-only) Returns the projection of this tile source. */
        readonly 'projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'projection.bounds': Bounds;
        readonly 'projection.name': string;
        /** Returns the property a rebuilt tile's features are ranked by. */
        'rankProperty': string;
    };
    'massif::PointGeometry': {
        /** (read-only) Returns the minimal bounds for the geometry. */
        readonly 'bounds': Bounds;
        readonly 'centerPos': Position;
        /** (read-only) Returns the geometry as a GeoJSON string, in its own coordinates. Here rather than only on Feature because serialising a shape otherwise means constructing a GeoJSONGeometryWriter, which no string-based binding can do. */
        readonly 'geoJSON': string;
        /** (read-only) Returns the position of the point. */
        readonly 'pos': Position;
        readonly 'type': 'GEOMETRY_TYPE_POINT' | 'GEOMETRY_TYPE_LINE' | 'GEOMETRY_TYPE_POLYGON' | 'GEOMETRY_TYPE_MULTIPOINT' | 'GEOMETRY_TYPE_MULTILINE' | 'GEOMETRY_TYPE_MULTIPOLYGON' | 'GEOMETRY_TYPE_COLLECTION' | number;
    };
    'massif::PointStyle': {
        /** (read-only) Returns the bitmap of the point. */
        readonly 'bitmap': Handle;
        /** (read-only) Returns the bytes per pixel parameter of this bitmap. Valid values are 1, 2, 3 and 4. */
        readonly 'bitmap.bytesPerPixel': number;
        /** (read-only) Returns the color format of this bitmap. */
        readonly 'bitmap.colorFormat': 'COLOR_FORMAT_UNSUPPORTED' | 'COLOR_FORMAT_BGRA' | 'COLOR_FORMAT_RGBA_4444' | 'COLOR_FORMAT_RGB_565' | number;
        /** (read-only) Returns the height of the bitmap. */
        readonly 'bitmap.height': number;
        /** (read-only) Returns the width of the bitmap. */
        readonly 'bitmap.width': number;
        /** (read-only) Returns the size of the point used for click detection. */
        readonly 'clickSize': number;
        /** (read-only) Returns the color of the vector element. */
        readonly 'color': number;
        /** (read-only) Returns the size of the point. */
        readonly 'size': number;
    };
    'massif::PointStyleBuilder': {
        /** Returns the bitmap of the point. */
        'bitmap': Handle;
        /** (read-only) Returns the bytes per pixel parameter of this bitmap. Valid values are 1, 2, 3 and 4. */
        readonly 'bitmap.bytesPerPixel': number;
        /** (read-only) Returns the color format of this bitmap. */
        readonly 'bitmap.colorFormat': 'COLOR_FORMAT_UNSUPPORTED' | 'COLOR_FORMAT_BGRA' | 'COLOR_FORMAT_RGBA_4444' | 'COLOR_FORMAT_RGB_565' | number;
        /** (read-only) Returns the height of the bitmap. */
        readonly 'bitmap.height': number;
        /** (read-only) Returns the width of the bitmap. */
        readonly 'bitmap.width': number;
        /** Returns the size of the point used for click detection. */
        'clickSize': number;
        /** Returns the color of the vector element. */
        'color': number;
        /** Returns the size of the point. */
        'size': number;
    };
    'massif::Polygon': {
        /** (read-only) Returns the bounds of this vector element. */
        readonly 'bounds': Bounds;
        'geometry': Handle;
        /** (read-only) Returns the minimal bounds for the geometry. */
        readonly 'geometry.bounds': Bounds;
        readonly 'geometry.centerPos': Position;
        /** (read-only) Returns the geometry as a GeoJSON string, in its own coordinates. Here rather than only on Feature because serialising a shape otherwise means constructing a GeoJSONGeometryWriter, which no string-based binding can do. */
        readonly 'geometry.geoJSON': string;
        /** (read-only) Returns the list of map position lists defining the inner rings of the polygon (holes). */
        readonly 'geometry.holes': Json;
        /** (read-only) Returns the list of map positions defining the outer ring of the polygon. */
        readonly 'geometry.poses': Json;
        /** (read-only) Returns the list of map position lists defining the rings of the polygon. */
        readonly 'geometry.rings': Json;
        readonly 'geometry.type': 'GEOMETRY_TYPE_POINT' | 'GEOMETRY_TYPE_LINE' | 'GEOMETRY_TYPE_POLYGON' | 'GEOMETRY_TYPE_MULTIPOINT' | 'GEOMETRY_TYPE_MULTILINE' | 'GEOMETRY_TYPE_MULTIPOLYGON' | 'GEOMETRY_TYPE_COLLECTION' | number;
        /** Returns the internal id of this vector element. */
        'id': number;
        /** Returns a copy of the vector element meta data map. The changes you make to this map are NOT reflected in the actual meta data of the element. */
        'metaData': Record<string, Json>;
        /** Returns a copy of the vector element meta data map. The changes you make to this map are NOT reflected in the actual meta data of the element. */
        [key: `metaData.${string}`]: Json;
        /** Returns the style of this polygon. */
        'style': Handle;
        /** (read-only) Returns the color of the vector element. */
        readonly 'style.color': number;
        /** (read-only) Returns the style of the edges of the polygon. */
        readonly 'style.lineStyle': Handle;
        /** (read-only) Returns the bitmap of the line. */
        readonly 'style.lineStyle.bitmap': Handle;
        /** (read-only) Returns the width of the line used for click detection. */
        readonly 'style.lineStyle.clickWidth': number;
        /** (read-only) Returns the color of the vector element. */
        readonly 'style.lineStyle.color': number;
        /** (read-only) Returns the end point type of the line. */
        readonly 'style.lineStyle.lineEndType': 'LINE_END_TYPE_NONE' | 'LINE_END_TYPE_SQUARE' | 'LINE_END_TYPE_ROUND' | number;
        /** (read-only) Returns the join type of the line. */
        readonly 'style.lineStyle.lineJoinType': 'LINE_JOIN_TYPE_NONE' | 'LINE_JOIN_TYPE_MITER' | 'LINE_JOIN_TYPE_BEVEL' | 'LINE_JOIN_TYPE_ROUND' | number;
        /** (read-only) Returns the stretching factor of the line. */
        readonly 'style.lineStyle.stretchFactor': number;
        /** (read-only) Returns the width of the line. */
        readonly 'style.lineStyle.width': number;
        /** Returns the state of the visibility flag of this vector element. */
        'visible': boolean;
    };
    'massif::Polygon3D': {
        /** (read-only) Returns the bounds of this vector element. */
        readonly 'bounds': Bounds;
        'geometry': Handle;
        /** (read-only) Returns the minimal bounds for the geometry. */
        readonly 'geometry.bounds': Bounds;
        readonly 'geometry.centerPos': Position;
        /** (read-only) Returns the geometry as a GeoJSON string, in its own coordinates. Here rather than only on Feature because serialising a shape otherwise means constructing a GeoJSONGeometryWriter, which no string-based binding can do. */
        readonly 'geometry.geoJSON': string;
        /** (read-only) Returns the list of map position lists defining the inner rings of the polygon (holes). */
        readonly 'geometry.holes': Json;
        /** (read-only) Returns the list of map positions defining the outer ring of the polygon. */
        readonly 'geometry.poses': Json;
        /** (read-only) Returns the list of map position lists defining the rings of the polygon. */
        readonly 'geometry.rings': Json;
        readonly 'geometry.type': 'GEOMETRY_TYPE_POINT' | 'GEOMETRY_TYPE_LINE' | 'GEOMETRY_TYPE_POLYGON' | 'GEOMETRY_TYPE_MULTIPOINT' | 'GEOMETRY_TYPE_MULTILINE' | 'GEOMETRY_TYPE_MULTIPOLYGON' | 'GEOMETRY_TYPE_COLLECTION' | number;
        /** Returns the height of this 3d polygon. */
        'height': number;
        /** Returns the internal id of this vector element. */
        'id': number;
        /** Returns a copy of the vector element meta data map. The changes you make to this map are NOT reflected in the actual meta data of the element. */
        'metaData': Record<string, Json>;
        /** Returns a copy of the vector element meta data map. The changes you make to this map are NOT reflected in the actual meta data of the element. */
        [key: `metaData.${string}`]: Json;
        /** Returns the style of this 3d polygon. */
        'style': Handle;
        /** (read-only) Returns the color of the vector element. */
        readonly 'style.color': number;
        /** (read-only) Returns the color for sides of the 3d polygon. */
        readonly 'style.sideColor': number;
        /** Returns the state of the visibility flag of this vector element. */
        'visible': boolean;
    };
    'massif::Polygon3DStyle': {
        /** (read-only) Returns the color of the vector element. */
        readonly 'color': number;
        /** (read-only) Returns the color for sides of the 3d polygon. */
        readonly 'sideColor': number;
    };
    'massif::Polygon3DStyleBuilder': {
        /** Returns the color of the vector element. */
        'color': number;
        /** Returns the color of the 3d polygon sides. */
        'sideColor': number;
    };
    'massif::PolygonGeometry': {
        /** (read-only) Returns the minimal bounds for the geometry. */
        readonly 'bounds': Bounds;
        readonly 'centerPos': Position;
        /** (read-only) Returns the geometry as a GeoJSON string, in its own coordinates. Here rather than only on Feature because serialising a shape otherwise means constructing a GeoJSONGeometryWriter, which no string-based binding can do. */
        readonly 'geoJSON': string;
        /** (read-only) Returns the list of map position lists defining the inner rings of the polygon (holes). */
        readonly 'holes': Json;
        /** (read-only) Returns the list of map positions defining the outer ring of the polygon. */
        readonly 'poses': Json;
        /** (read-only) Returns the list of map position lists defining the rings of the polygon. */
        readonly 'rings': Json;
        readonly 'type': 'GEOMETRY_TYPE_POINT' | 'GEOMETRY_TYPE_LINE' | 'GEOMETRY_TYPE_POLYGON' | 'GEOMETRY_TYPE_MULTIPOINT' | 'GEOMETRY_TYPE_MULTILINE' | 'GEOMETRY_TYPE_MULTIPOLYGON' | 'GEOMETRY_TYPE_COLLECTION' | number;
    };
    'massif::PolygonStyle': {
        /** (read-only) Returns the color of the vector element. */
        readonly 'color': number;
        /** (read-only) Returns the style of the edges of the polygon. */
        readonly 'lineStyle': Handle;
        /** (read-only) Returns the bitmap of the line. */
        readonly 'lineStyle.bitmap': Handle;
        /** (read-only) Returns the bytes per pixel parameter of this bitmap. Valid values are 1, 2, 3 and 4. */
        readonly 'lineStyle.bitmap.bytesPerPixel': number;
        /** (read-only) Returns the color format of this bitmap. */
        readonly 'lineStyle.bitmap.colorFormat': 'COLOR_FORMAT_UNSUPPORTED' | 'COLOR_FORMAT_BGRA' | 'COLOR_FORMAT_RGBA_4444' | 'COLOR_FORMAT_RGB_565' | number;
        /** (read-only) Returns the height of the bitmap. */
        readonly 'lineStyle.bitmap.height': number;
        /** (read-only) Returns the width of the bitmap. */
        readonly 'lineStyle.bitmap.width': number;
        /** (read-only) Returns the width of the line used for click detection. */
        readonly 'lineStyle.clickWidth': number;
        /** (read-only) Returns the color of the vector element. */
        readonly 'lineStyle.color': number;
        /** (read-only) Returns the end point type of the line. */
        readonly 'lineStyle.lineEndType': 'LINE_END_TYPE_NONE' | 'LINE_END_TYPE_SQUARE' | 'LINE_END_TYPE_ROUND' | number;
        /** (read-only) Returns the join type of the line. */
        readonly 'lineStyle.lineJoinType': 'LINE_JOIN_TYPE_NONE' | 'LINE_JOIN_TYPE_MITER' | 'LINE_JOIN_TYPE_BEVEL' | 'LINE_JOIN_TYPE_ROUND' | number;
        /** (read-only) Returns the stretching factor of the line. */
        readonly 'lineStyle.stretchFactor': number;
        /** (read-only) Returns the width of the line. */
        readonly 'lineStyle.width': number;
    };
    'massif::PolygonStyleBuilder': {
        /** Returns the color of the vector element. */
        'color': number;
        /** Returns the line style of the edges of the polygon. */
        'lineStyle': Handle;
        /** (read-only) Returns the bitmap of the line. */
        readonly 'lineStyle.bitmap': Handle;
        /** (read-only) Returns the bytes per pixel parameter of this bitmap. Valid values are 1, 2, 3 and 4. */
        readonly 'lineStyle.bitmap.bytesPerPixel': number;
        /** (read-only) Returns the color format of this bitmap. */
        readonly 'lineStyle.bitmap.colorFormat': 'COLOR_FORMAT_UNSUPPORTED' | 'COLOR_FORMAT_BGRA' | 'COLOR_FORMAT_RGBA_4444' | 'COLOR_FORMAT_RGB_565' | number;
        /** (read-only) Returns the height of the bitmap. */
        readonly 'lineStyle.bitmap.height': number;
        /** (read-only) Returns the width of the bitmap. */
        readonly 'lineStyle.bitmap.width': number;
        /** (read-only) Returns the width of the line used for click detection. */
        readonly 'lineStyle.clickWidth': number;
        /** (read-only) Returns the color of the vector element. */
        readonly 'lineStyle.color': number;
        /** (read-only) Returns the end point type of the line. */
        readonly 'lineStyle.lineEndType': 'LINE_END_TYPE_NONE' | 'LINE_END_TYPE_SQUARE' | 'LINE_END_TYPE_ROUND' | number;
        /** (read-only) Returns the join type of the line. */
        readonly 'lineStyle.lineJoinType': 'LINE_JOIN_TYPE_NONE' | 'LINE_JOIN_TYPE_MITER' | 'LINE_JOIN_TYPE_BEVEL' | 'LINE_JOIN_TYPE_ROUND' | number;
        /** (read-only) Returns the stretching factor of the line. */
        readonly 'lineStyle.stretchFactor': number;
        /** (read-only) Returns the width of the line. */
        readonly 'lineStyle.width': number;
    };
    'massif::Popup': {
        /** Returns the horizontal anchor point of this popup. */
        'anchorPointX': number;
        /** Returns the vertical anchor point of this popup. */
        'anchorPointY': number;
        /** Returns the base billboard this billboard is attached to. */
        'baseBillboard': Handle;
        /** Returns the base billboard this billboard is attached to. */
        'baseBillboard.baseBillboard': Handle;
        /** (read-only) Returns the bounds of this billboard or the base billboard, if there is one. */
        readonly 'baseBillboard.bounds': Bounds;
        /** Returns the geometry object that defines the location of this billboard. */
        'baseBillboard.geometry': Handle;
        /** (read-only) Returns the minimal bounds for the geometry. */
        readonly 'baseBillboard.geometry.bounds': Bounds;
        readonly 'baseBillboard.geometry.centerPos': Position;
        /** (read-only) Returns the geometry as a GeoJSON string, in its own coordinates. Here rather than only on Feature because serialising a shape otherwise means constructing a GeoJSONGeometryWriter, which no string-based binding can do. */
        readonly 'baseBillboard.geometry.geoJSON': string;
        readonly 'baseBillboard.geometry.type': 'GEOMETRY_TYPE_POINT' | 'GEOMETRY_TYPE_LINE' | 'GEOMETRY_TYPE_POLYGON' | 'GEOMETRY_TYPE_MULTIPOINT' | 'GEOMETRY_TYPE_MULTILINE' | 'GEOMETRY_TYPE_MULTIPOLYGON' | 'GEOMETRY_TYPE_COLLECTION' | number;
        /** Returns the internal id of this vector element. */
        'baseBillboard.id': number;
        /** Returns a copy of the vector element meta data map. The changes you make to this map are NOT reflected in the actual meta data of the element. */
        'baseBillboard.metaData': Record<string, Json>;
        /** Returns a copy of the vector element meta data map. The changes you make to this map are NOT reflected in the actual meta data of the element. */
        [key: `baseBillboard.metaData.${string}`]: Json;
        /** (read-only) Returns the location of the root billboard: getGeometry() if this billboard has a location, otherwise the location found by following the chain of base billboards to its root. */
        readonly 'baseBillboard.rootGeometry': Handle;
        /** (read-only) Returns the minimal bounds for the geometry. */
        readonly 'baseBillboard.rootGeometry.bounds': Bounds;
        readonly 'baseBillboard.rootGeometry.centerPos': Position;
        /** (read-only) Returns the geometry as a GeoJSON string, in its own coordinates. Here rather than only on Feature because serialising a shape otherwise means constructing a GeoJSONGeometryWriter, which no string-based binding can do. */
        readonly 'baseBillboard.rootGeometry.geoJSON': string;
        readonly 'baseBillboard.rootGeometry.type': 'GEOMETRY_TYPE_POINT' | 'GEOMETRY_TYPE_LINE' | 'GEOMETRY_TYPE_POLYGON' | 'GEOMETRY_TYPE_MULTIPOINT' | 'GEOMETRY_TYPE_MULTILINE' | 'GEOMETRY_TYPE_MULTIPOLYGON' | 'GEOMETRY_TYPE_COLLECTION' | number;
        /** Returns the rotation angle of this billboard. */
        'baseBillboard.rotation': number;
        /** Returns the state of the visibility flag of this vector element. */
        'baseBillboard.visible': boolean;
        /** (read-only) Returns the bounds of this billboard or the base billboard, if there is one. */
        readonly 'bounds': Bounds;
        /** Returns the geometry object that defines the location of this billboard. */
        'geometry': Handle;
        /** (read-only) Returns the minimal bounds for the geometry. */
        readonly 'geometry.bounds': Bounds;
        readonly 'geometry.centerPos': Position;
        /** (read-only) Returns the geometry as a GeoJSON string, in its own coordinates. Here rather than only on Feature because serialising a shape otherwise means constructing a GeoJSONGeometryWriter, which no string-based binding can do. */
        readonly 'geometry.geoJSON': string;
        readonly 'geometry.type': 'GEOMETRY_TYPE_POINT' | 'GEOMETRY_TYPE_LINE' | 'GEOMETRY_TYPE_POLYGON' | 'GEOMETRY_TYPE_MULTIPOINT' | 'GEOMETRY_TYPE_MULTILINE' | 'GEOMETRY_TYPE_MULTIPOLYGON' | 'GEOMETRY_TYPE_COLLECTION' | number;
        /** Returns the internal id of this vector element. */
        'id': number;
        /** Returns a copy of the vector element meta data map. The changes you make to this map are NOT reflected in the actual meta data of the element. */
        'metaData': Record<string, Json>;
        /** Returns a copy of the vector element meta data map. The changes you make to this map are NOT reflected in the actual meta data of the element. */
        [key: `metaData.${string}`]: Json;
        /** (read-only) Returns the location of the root billboard: getGeometry() if this billboard has a location, otherwise the location found by following the chain of base billboards to its root. */
        readonly 'rootGeometry': Handle;
        /** (read-only) Returns the minimal bounds for the geometry. */
        readonly 'rootGeometry.bounds': Bounds;
        readonly 'rootGeometry.centerPos': Position;
        /** (read-only) Returns the geometry as a GeoJSON string, in its own coordinates. Here rather than only on Feature because serialising a shape otherwise means constructing a GeoJSONGeometryWriter, which no string-based binding can do. */
        readonly 'rootGeometry.geoJSON': string;
        readonly 'rootGeometry.type': 'GEOMETRY_TYPE_POINT' | 'GEOMETRY_TYPE_LINE' | 'GEOMETRY_TYPE_POLYGON' | 'GEOMETRY_TYPE_MULTIPOINT' | 'GEOMETRY_TYPE_MULTILINE' | 'GEOMETRY_TYPE_MULTIPOLYGON' | 'GEOMETRY_TYPE_COLLECTION' | number;
        /** Returns the rotation angle of this billboard. */
        'rotation': number;
        /** Returns the style of this Popup. */
        'style': Handle;
        /** (read-only) Returns the animation style of the billboard. */
        readonly 'style.animationStyle': Handle;
        /** (read-only) Returns the fade animation type. */
        readonly 'style.animationStyle.fadeAnimationType': 'ANIMATION_TYPE_NONE' | 'ANIMATION_TYPE_STEP' | 'ANIMATION_TYPE_LINEAR' | 'ANIMATION_TYPE_SMOOTHSTEP' | 'ANIMATION_TYPE_SPRING' | number;
        /** (read-only) Returns the phase-in duration of the animation. */
        readonly 'style.animationStyle.phaseInDuration': number;
        /** (read-only) Returns the phase-out duration of the animation. */
        readonly 'style.animationStyle.phaseOutDuration': number;
        /** (read-only) Returns the relative speed of the animation. */
        readonly 'style.animationStyle.relativeSpeed': number;
        /** (read-only) Returns the size-related animation type. */
        readonly 'style.animationStyle.sizeAnimationType': 'ANIMATION_TYPE_NONE' | 'ANIMATION_TYPE_STEP' | 'ANIMATION_TYPE_LINEAR' | 'ANIMATION_TYPE_SMOOTHSTEP' | 'ANIMATION_TYPE_SPRING' | number;
        /** (read-only) Returns the horizontal attaching anchor point of the billoard. */
        readonly 'style.attachAnchorPointX': number;
        /** (read-only) Returns the vertical attaching anchor point of the billoard. */
        readonly 'style.attachAnchorPointY': number;
        /** (read-only) Returns the state of the causes overlap flag. */
        readonly 'style.causesOverlap': boolean;
        /** (read-only) Returns the color of the vector element. */
        readonly 'style.color': number;
        /** (read-only) Returns the state of the allow overlap flag. */
        readonly 'style.hideIfOverlapped': boolean;
        /** (read-only) Returns the horizontal offset of the billboard. */
        readonly 'style.horizontalOffset': number;
        /** (read-only) Returns the placement priority of the billboard. */
        readonly 'style.placementPriority': number;
        /** (read-only) Returns the state of the scale with DPI flag. */
        readonly 'style.scaleWithDPI': boolean;
        /** (read-only) Returns the vertical offset of the billboard. */
        readonly 'style.verticalOffset': number;
        /** Returns the state of the visibility flag of this vector element. */
        'visible': boolean;
    };
    'massif::PopupClickInfo': {
        /** (read-only) Returns the click info. */
        readonly 'clickInfo': ClickInfo;
        /** (read-only) Returns the click position. */
        readonly 'clickPos': Position;
        /** (read-only) Returns the click type. */
        readonly 'clickType': 'CLICK_TYPE_SINGLE' | 'CLICK_TYPE_LONG' | 'CLICK_TYPE_DOUBLE' | 'CLICK_TYPE_DUAL' | number;
        /** (read-only) Returns the 2D click position on the clicked popup. */
        readonly 'elementClickPos': [number, number];
        /** (read-only) Returns the clicked popup. */
        readonly 'popup': Handle;
        /** Returns the horizontal anchor point of this popup. */
        'popup.anchorPointX': number;
        /** Returns the vertical anchor point of this popup. */
        'popup.anchorPointY': number;
        /** Returns the base billboard this billboard is attached to. */
        'popup.baseBillboard': Handle;
        /** Returns the base billboard this billboard is attached to. */
        'popup.baseBillboard.baseBillboard': Handle;
        /** (read-only) Returns the bounds of this billboard or the base billboard, if there is one. */
        readonly 'popup.baseBillboard.bounds': Bounds;
        /** Returns the geometry object that defines the location of this billboard. */
        'popup.baseBillboard.geometry': Handle;
        /** Returns the internal id of this vector element. */
        'popup.baseBillboard.id': number;
        /** Returns a copy of the vector element meta data map. The changes you make to this map are NOT reflected in the actual meta data of the element. */
        'popup.baseBillboard.metaData': Record<string, Json>;
        /** Returns a copy of the vector element meta data map. The changes you make to this map are NOT reflected in the actual meta data of the element. */
        [key: `popup.baseBillboard.metaData.${string}`]: Json;
        /** (read-only) Returns the location of the root billboard: getGeometry() if this billboard has a location, otherwise the location found by following the chain of base billboards to its root. */
        readonly 'popup.baseBillboard.rootGeometry': Handle;
        /** Returns the rotation angle of this billboard. */
        'popup.baseBillboard.rotation': number;
        /** Returns the state of the visibility flag of this vector element. */
        'popup.baseBillboard.visible': boolean;
        /** (read-only) Returns the bounds of this billboard or the base billboard, if there is one. */
        readonly 'popup.bounds': Bounds;
        /** Returns the geometry object that defines the location of this billboard. */
        'popup.geometry': Handle;
        /** (read-only) Returns the minimal bounds for the geometry. */
        readonly 'popup.geometry.bounds': Bounds;
        readonly 'popup.geometry.centerPos': Position;
        /** (read-only) Returns the geometry as a GeoJSON string, in its own coordinates. Here rather than only on Feature because serialising a shape otherwise means constructing a GeoJSONGeometryWriter, which no string-based binding can do. */
        readonly 'popup.geometry.geoJSON': string;
        readonly 'popup.geometry.type': 'GEOMETRY_TYPE_POINT' | 'GEOMETRY_TYPE_LINE' | 'GEOMETRY_TYPE_POLYGON' | 'GEOMETRY_TYPE_MULTIPOINT' | 'GEOMETRY_TYPE_MULTILINE' | 'GEOMETRY_TYPE_MULTIPOLYGON' | 'GEOMETRY_TYPE_COLLECTION' | number;
        /** Returns the internal id of this vector element. */
        'popup.id': number;
        /** Returns a copy of the vector element meta data map. The changes you make to this map are NOT reflected in the actual meta data of the element. */
        'popup.metaData': Record<string, Json>;
        /** Returns a copy of the vector element meta data map. The changes you make to this map are NOT reflected in the actual meta data of the element. */
        [key: `popup.metaData.${string}`]: Json;
        /** (read-only) Returns the location of the root billboard: getGeometry() if this billboard has a location, otherwise the location found by following the chain of base billboards to its root. */
        readonly 'popup.rootGeometry': Handle;
        /** (read-only) Returns the minimal bounds for the geometry. */
        readonly 'popup.rootGeometry.bounds': Bounds;
        readonly 'popup.rootGeometry.centerPos': Position;
        /** (read-only) Returns the geometry as a GeoJSON string, in its own coordinates. Here rather than only on Feature because serialising a shape otherwise means constructing a GeoJSONGeometryWriter, which no string-based binding can do. */
        readonly 'popup.rootGeometry.geoJSON': string;
        readonly 'popup.rootGeometry.type': 'GEOMETRY_TYPE_POINT' | 'GEOMETRY_TYPE_LINE' | 'GEOMETRY_TYPE_POLYGON' | 'GEOMETRY_TYPE_MULTIPOINT' | 'GEOMETRY_TYPE_MULTILINE' | 'GEOMETRY_TYPE_MULTIPOLYGON' | 'GEOMETRY_TYPE_COLLECTION' | number;
        /** Returns the rotation angle of this billboard. */
        'popup.rotation': number;
        /** Returns the style of this Popup. */
        'popup.style': Handle;
        /** (read-only) Returns the animation style of the billboard. */
        readonly 'popup.style.animationStyle': Handle;
        /** (read-only) Returns the horizontal attaching anchor point of the billoard. */
        readonly 'popup.style.attachAnchorPointX': number;
        /** (read-only) Returns the vertical attaching anchor point of the billoard. */
        readonly 'popup.style.attachAnchorPointY': number;
        /** (read-only) Returns the state of the causes overlap flag. */
        readonly 'popup.style.causesOverlap': boolean;
        /** (read-only) Returns the color of the vector element. */
        readonly 'popup.style.color': number;
        /** (read-only) Returns the state of the allow overlap flag. */
        readonly 'popup.style.hideIfOverlapped': boolean;
        /** (read-only) Returns the horizontal offset of the billboard. */
        readonly 'popup.style.horizontalOffset': number;
        /** (read-only) Returns the placement priority of the billboard. */
        readonly 'popup.style.placementPriority': number;
        /** (read-only) Returns the state of the scale with DPI flag. */
        readonly 'popup.style.scaleWithDPI': boolean;
        /** (read-only) Returns the vertical offset of the billboard. */
        readonly 'popup.style.verticalOffset': number;
        /** Returns the state of the visibility flag of this vector element. */
        'popup.visible': boolean;
    };
    'massif::PopupDrawInfo': {
        /** (read-only) Returns the value used for converting display independent pixels (dp) to pixels (px). */
        readonly 'DPToPX': number;
        /** (read-only) Returns the screen position of the anchor point of this popup in pixels. */
        readonly 'anchorScreenPos': [number, number];
        /** (read-only) Returns the popup to draw. */
        readonly 'popup': Handle;
        /** Returns the horizontal anchor point of this popup. */
        'popup.anchorPointX': number;
        /** Returns the vertical anchor point of this popup. */
        'popup.anchorPointY': number;
        /** Returns the base billboard this billboard is attached to. */
        'popup.baseBillboard': Handle;
        /** Returns the base billboard this billboard is attached to. */
        'popup.baseBillboard.baseBillboard': Handle;
        /** (read-only) Returns the bounds of this billboard or the base billboard, if there is one. */
        readonly 'popup.baseBillboard.bounds': Bounds;
        /** Returns the geometry object that defines the location of this billboard. */
        'popup.baseBillboard.geometry': Handle;
        /** Returns the internal id of this vector element. */
        'popup.baseBillboard.id': number;
        /** Returns a copy of the vector element meta data map. The changes you make to this map are NOT reflected in the actual meta data of the element. */
        'popup.baseBillboard.metaData': Record<string, Json>;
        /** Returns a copy of the vector element meta data map. The changes you make to this map are NOT reflected in the actual meta data of the element. */
        [key: `popup.baseBillboard.metaData.${string}`]: Json;
        /** (read-only) Returns the location of the root billboard: getGeometry() if this billboard has a location, otherwise the location found by following the chain of base billboards to its root. */
        readonly 'popup.baseBillboard.rootGeometry': Handle;
        /** Returns the rotation angle of this billboard. */
        'popup.baseBillboard.rotation': number;
        /** Returns the state of the visibility flag of this vector element. */
        'popup.baseBillboard.visible': boolean;
        /** (read-only) Returns the bounds of this billboard or the base billboard, if there is one. */
        readonly 'popup.bounds': Bounds;
        /** Returns the geometry object that defines the location of this billboard. */
        'popup.geometry': Handle;
        /** (read-only) Returns the minimal bounds for the geometry. */
        readonly 'popup.geometry.bounds': Bounds;
        readonly 'popup.geometry.centerPos': Position;
        /** (read-only) Returns the geometry as a GeoJSON string, in its own coordinates. Here rather than only on Feature because serialising a shape otherwise means constructing a GeoJSONGeometryWriter, which no string-based binding can do. */
        readonly 'popup.geometry.geoJSON': string;
        readonly 'popup.geometry.type': 'GEOMETRY_TYPE_POINT' | 'GEOMETRY_TYPE_LINE' | 'GEOMETRY_TYPE_POLYGON' | 'GEOMETRY_TYPE_MULTIPOINT' | 'GEOMETRY_TYPE_MULTILINE' | 'GEOMETRY_TYPE_MULTIPOLYGON' | 'GEOMETRY_TYPE_COLLECTION' | number;
        /** Returns the internal id of this vector element. */
        'popup.id': number;
        /** Returns a copy of the vector element meta data map. The changes you make to this map are NOT reflected in the actual meta data of the element. */
        'popup.metaData': Record<string, Json>;
        /** Returns a copy of the vector element meta data map. The changes you make to this map are NOT reflected in the actual meta data of the element. */
        [key: `popup.metaData.${string}`]: Json;
        /** (read-only) Returns the location of the root billboard: getGeometry() if this billboard has a location, otherwise the location found by following the chain of base billboards to its root. */
        readonly 'popup.rootGeometry': Handle;
        /** (read-only) Returns the minimal bounds for the geometry. */
        readonly 'popup.rootGeometry.bounds': Bounds;
        readonly 'popup.rootGeometry.centerPos': Position;
        /** (read-only) Returns the geometry as a GeoJSON string, in its own coordinates. Here rather than only on Feature because serialising a shape otherwise means constructing a GeoJSONGeometryWriter, which no string-based binding can do. */
        readonly 'popup.rootGeometry.geoJSON': string;
        readonly 'popup.rootGeometry.type': 'GEOMETRY_TYPE_POINT' | 'GEOMETRY_TYPE_LINE' | 'GEOMETRY_TYPE_POLYGON' | 'GEOMETRY_TYPE_MULTIPOINT' | 'GEOMETRY_TYPE_MULTILINE' | 'GEOMETRY_TYPE_MULTIPOLYGON' | 'GEOMETRY_TYPE_COLLECTION' | number;
        /** Returns the rotation angle of this billboard. */
        'popup.rotation': number;
        /** Returns the style of this Popup. */
        'popup.style': Handle;
        /** (read-only) Returns the animation style of the billboard. */
        readonly 'popup.style.animationStyle': Handle;
        /** (read-only) Returns the horizontal attaching anchor point of the billoard. */
        readonly 'popup.style.attachAnchorPointX': number;
        /** (read-only) Returns the vertical attaching anchor point of the billoard. */
        readonly 'popup.style.attachAnchorPointY': number;
        /** (read-only) Returns the state of the causes overlap flag. */
        readonly 'popup.style.causesOverlap': boolean;
        /** (read-only) Returns the color of the vector element. */
        readonly 'popup.style.color': number;
        /** (read-only) Returns the state of the allow overlap flag. */
        readonly 'popup.style.hideIfOverlapped': boolean;
        /** (read-only) Returns the horizontal offset of the billboard. */
        readonly 'popup.style.horizontalOffset': number;
        /** (read-only) Returns the placement priority of the billboard. */
        readonly 'popup.style.placementPriority': number;
        /** (read-only) Returns the state of the scale with DPI flag. */
        readonly 'popup.style.scaleWithDPI': boolean;
        /** (read-only) Returns the vertical offset of the billboard. */
        readonly 'popup.style.verticalOffset': number;
        /** Returns the state of the visibility flag of this vector element. */
        'popup.visible': boolean;
        /** (read-only) Returns the screen bounds, so that the popup can be clipped if neccessary. */
        readonly 'screenBounds': Json;
    };
    'massif::PopupStyle': {
        /** (read-only) Returns the animation style of the billboard. */
        readonly 'animationStyle': Handle;
        /** (read-only) Returns the fade animation type. */
        readonly 'animationStyle.fadeAnimationType': 'ANIMATION_TYPE_NONE' | 'ANIMATION_TYPE_STEP' | 'ANIMATION_TYPE_LINEAR' | 'ANIMATION_TYPE_SMOOTHSTEP' | 'ANIMATION_TYPE_SPRING' | number;
        /** (read-only) Returns the phase-in duration of the animation. */
        readonly 'animationStyle.phaseInDuration': number;
        /** (read-only) Returns the phase-out duration of the animation. */
        readonly 'animationStyle.phaseOutDuration': number;
        /** (read-only) Returns the relative speed of the animation. */
        readonly 'animationStyle.relativeSpeed': number;
        /** (read-only) Returns the size-related animation type. */
        readonly 'animationStyle.sizeAnimationType': 'ANIMATION_TYPE_NONE' | 'ANIMATION_TYPE_STEP' | 'ANIMATION_TYPE_LINEAR' | 'ANIMATION_TYPE_SMOOTHSTEP' | 'ANIMATION_TYPE_SPRING' | number;
        /** (read-only) Returns the horizontal attaching anchor point of the billoard. */
        readonly 'attachAnchorPointX': number;
        /** (read-only) Returns the vertical attaching anchor point of the billoard. */
        readonly 'attachAnchorPointY': number;
        /** (read-only) Returns the state of the causes overlap flag. */
        readonly 'causesOverlap': boolean;
        /** (read-only) Returns the color of the vector element. */
        readonly 'color': number;
        /** (read-only) Returns the state of the allow overlap flag. */
        readonly 'hideIfOverlapped': boolean;
        /** (read-only) Returns the horizontal offset of the billboard. */
        readonly 'horizontalOffset': number;
        /** (read-only) Returns the placement priority of the billboard. */
        readonly 'placementPriority': number;
        /** (read-only) Returns the state of the scale with DPI flag. */
        readonly 'scaleWithDPI': boolean;
        /** (read-only) Returns the vertical offset of the billboard. */
        readonly 'verticalOffset': number;
    };
    'massif::PopupStyleBuilder': {
        /** Returns the animation style of the billboard. */
        'animationStyle': Handle;
        /** (read-only) Returns the fade animation type. */
        readonly 'animationStyle.fadeAnimationType': 'ANIMATION_TYPE_NONE' | 'ANIMATION_TYPE_STEP' | 'ANIMATION_TYPE_LINEAR' | 'ANIMATION_TYPE_SMOOTHSTEP' | 'ANIMATION_TYPE_SPRING' | number;
        /** (read-only) Returns the phase-in duration of the animation. */
        readonly 'animationStyle.phaseInDuration': number;
        /** (read-only) Returns the phase-out duration of the animation. */
        readonly 'animationStyle.phaseOutDuration': number;
        /** (read-only) Returns the relative speed of the animation. */
        readonly 'animationStyle.relativeSpeed': number;
        /** (read-only) Returns the size-related animation type. */
        readonly 'animationStyle.sizeAnimationType': 'ANIMATION_TYPE_NONE' | 'ANIMATION_TYPE_STEP' | 'ANIMATION_TYPE_LINEAR' | 'ANIMATION_TYPE_SMOOTHSTEP' | 'ANIMATION_TYPE_SPRING' | number;
        /** Returns the horizontal attaching anchor point of the billboard. */
        'attachAnchorPointX': number;
        /** Returns the vertical attaching anchor point of the billboard. */
        'attachAnchorPointY': number;
        /** Returns the state of the causes overlap flag. */
        'causesOverlap': boolean;
        /** Returns the color of the vector element. */
        'color': number;
        /** Returns the state of the allow overlap flag. */
        'hideIfOverlapped': boolean;
        /** Returns the horizontal offset of the billboard. */
        'horizontalOffset': number;
        /** Returns the placement priority of the billboard. */
        'placementPriority': number;
        /** Returns the state of the scale with DPI flag. */
        'scaleWithDPI': boolean;
        /** Returns the vertical offset of the billboard. */
        'verticalOffset': number;
    };
    'massif::PostProcessEffect': {
        /** (read-only) Returns the fragment shader source of the effect. */
        readonly 'fragmentShader': string;
        /** (read-only) Returns the name of the effect. */
        readonly 'name': string;
        /** Returns true if the effect needs the terrain depth pre-pass (uTerrainDepthTex). */
        'terrainDepthRequired': boolean;
        /** Returns true if the effect wants the terrain surface normal in the depth pre-pass. */
        'terrainNormalsRequired': boolean;
    };
    'massif::Projection': {
        /** (read-only) Returns the bounds of this projection. */
        readonly 'bounds': Bounds;
        readonly 'name': string;
    };
    'massif::RasterTileClickInfo': {
        /** (read-only) Returns the click info. */
        readonly 'clickInfo': ClickInfo;
        /** (read-only) Returns the click position. */
        readonly 'clickPos': Position;
        /** (read-only) Returns the click type. */
        readonly 'clickType': 'CLICK_TYPE_SINGLE' | 'CLICK_TYPE_LONG' | 'CLICK_TYPE_DOUBLE' | 'CLICK_TYPE_DUAL' | number;
        /** (read-only) Returns the interpolated color at the click position. */
        readonly 'interpolatedColor': number;
        /** (read-only) Returns the layer of the raster tile. */
        readonly 'layer': Handle;
        /** Returns the culling delay of the layer in milliseconds. */
        'layer.cullDelay': number;
        /** Returns a copy of the layer meta data map. The changes you make to this map are NOT reflected in the actual meta data of the layer. */
        'layer.metaData': Record<string, Json>;
        /** Returns a copy of the layer meta data map. The changes you make to this map are NOT reflected in the actual meta data of the layer. */
        [key: `layer.metaData.${string}`]: Json;
        /** Returns the opacity of this layer. */
        'layer.opacity': number;
        /** Returns whether this layer goes through the post-process effect. */
        'layer.postProcessed': boolean;
        /** Returns the layer task priority of this layer. */
        'layer.updatePriority': number;
        /** Returns the visibility of this layer. */
        'layer.visible': boolean;
        /** Returns the visible zoom range of this layer. */
        'layer.visibleZoomRange': [number, number];
        /** (read-only) Returns the tile id of the clicked feature. */
        readonly 'mapTile': Tile;
        /** (read-only) Returns the color of the nearest pixel to the click position. */
        readonly 'nearestColor': number;
    };
    'massif::RasterTileEventListener': {
    };
    'massif::RasterTileLayer': {
        /** Returns the tile data source of the associated UTF grid. By default this is null. */
        'UTFGridDataSource': Handle;
        /** (read-only) Returns the extent of the tiles in this data source. The bounds are in coordinate system of the projection of the data source. */
        readonly 'UTFGridDataSource.dataExtent': Bounds;
        /** Gets the current maximum overzoom level for this datasource. Over it the datasource will not be "drawn" */
        'UTFGridDataSource.maxOverzoomLevel': number;
        /** (read-only) Returns the maximum zoom level supported by this data source. */
        readonly 'UTFGridDataSource.maxZoom': number;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        'UTFGridDataSource.metaData': Record<string, Json>;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        [key: `UTFGridDataSource.metaData.${string}`]: Json;
        /** (read-only) Returns the minimum zoom level supported by this data source. */
        readonly 'UTFGridDataSource.minZoom': number;
        /** (read-only) Returns the projection of this tile source. */
        readonly 'UTFGridDataSource.projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'UTFGridDataSource.projection.bounds': Bounds;
        readonly 'UTFGridDataSource.projection.name': string;
        /** Returns the UTF grid event listener. */
        'UTFGridEventListener': Handle;
        /** Returns the culling delay of the layer in milliseconds. */
        'cullDelay': number;
        /** (read-only) Returns the data source assigned to this layer. */
        readonly 'dataSource': Handle;
        /** (read-only) Returns the extent of the tiles in this data source. The bounds are in coordinate system of the projection of the data source. */
        readonly 'dataSource.dataExtent': Bounds;
        /** Gets the current maximum overzoom level for this datasource. Over it the datasource will not be "drawn" */
        'dataSource.maxOverzoomLevel': number;
        /** (read-only) Returns the maximum zoom level supported by this data source. */
        readonly 'dataSource.maxZoom': number;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        'dataSource.metaData': Record<string, Json>;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        [key: `dataSource.metaData.${string}`]: Json;
        /** (read-only) Returns the minimum zoom level supported by this data source. */
        readonly 'dataSource.minZoom': number;
        /** (read-only) Returns the projection of this tile source. */
        readonly 'dataSource.projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'dataSource.projection.bounds': Bounds;
        readonly 'dataSource.projection.name': string;
        /** Returns the current frame number. */
        'frameNr': number;
        /** Gets the current maximum overzoom level for this layer. */
        'maxOverzoomLevel': number;
        /** Gets the current maximum underzoom level for this layer. */
        'maxUnderzoomLevel': number;
        /** Returns a copy of the layer meta data map. The changes you make to this map are NOT reflected in the actual meta data of the layer. */
        'metaData': Record<string, Json>;
        /** Returns a copy of the layer meta data map. The changes you make to this map are NOT reflected in the actual meta data of the layer. */
        [key: `metaData.${string}`]: Json;
        /** Returns the opacity of this layer. */
        'opacity': number;
        /** Returns whether this layer goes through the post-process effect. */
        'postProcessed': boolean;
        /** Returns the state of the preloading flag of this layer. */
        'preloading': boolean;
        readonly 'preloadingTileCount': number;
        /** (read-only) Returns the projection this layer's data is in, which is its data source's. */
        readonly 'projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'projection.bounds': Bounds;
        readonly 'projection.name': string;
        /** Returns the raster tile event listener. */
        'rasterTileEventListener': Handle;
        /** (read-only) Returns the data source assigned to this layer. */
        readonly 'source': Handle;
        /** (read-only) Returns the extent of the tiles in this data source. The bounds are in coordinate system of the projection of the data source. */
        readonly 'source.dataExtent': Bounds;
        /** Gets the current maximum overzoom level for this datasource. Over it the datasource will not be "drawn" */
        'source.maxOverzoomLevel': number;
        /** (read-only) Returns the maximum zoom level supported by this data source. */
        readonly 'source.maxZoom': number;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        'source.metaData': Record<string, Json>;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        [key: `source.metaData.${string}`]: Json;
        /** (read-only) Returns the minimum zoom level supported by this data source. */
        readonly 'source.minZoom': number;
        /** (read-only) Returns the projection of this tile source. */
        readonly 'source.projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'source.projection.bounds': Bounds;
        readonly 'source.projection.name': string;
        /** Returns the state of the synchronized refresh flag. */
        'synchronizedRefresh': boolean;
        /** Returns the tile texture cache capacity. */
        'textureCacheCapacity': number;
        /** Returns the current relative tile blending speed. */
        'tileBlendingSpeed': number;
        /** Returns the current tile filter mode. */
        'tileFilterMode': 'RASTER_TILE_FILTER_MODE_NEAREST' | 'RASTER_TILE_FILTER_MODE_BILINEAR' | 'RASTER_TILE_FILTER_MODE_BICUBIC' | number;
        /** Returns the tile load listener. */
        'tileLoadListener': Handle;
        /** Returns the current tile substitution policy. */
        'tileSubstitutionPolicy': 'TILE_SUBSTITUTION_POLICY_ALL' | 'TILE_SUBSTITUTION_POLICY_VISIBLE' | 'TILE_SUBSTITUTION_POLICY_NONE' | number;
        /** Returns the layer task priority of this layer. */
        'updatePriority': number;
        /** Returns the visibility of this layer. */
        'visible': boolean;
        /** (read-only) How many tiles the last cull put on screen. A diagnostic: it is what the tile LOD numbers actually cost. */
        readonly 'visibleTileCount': number;
        /** Returns the visible zoom range of this layer. */
        'visibleZoomRange': [number, number];
        /** Gets the current zoom level bias for this layer. */
        'zoomLevelBias': number;
    };
    'massif::RedrawRequestListener': {
    };
    'massif::RendererCaptureListener': {
    };
    'massif::ReverseGeocodingRequest': {
        /** (read-only) Returns the location of the query. */
        readonly 'location': Position;
        /** (read-only) Returns the projection of the query. */
        readonly 'projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'projection.bounds': Bounds;
        readonly 'projection.name': string;
        /** Returns the search radius (in meters). */
        'searchRadius': number;
    };
    'massif::ReverseGeocodingService': {
        'language': string;
    };
    'massif::RouteMatchingEdge': {
    };
    'massif::RouteMatchingPoint': {
        /** (read-only) Returns the corresponding matching edge index in the matching result. */
        readonly 'edgeIndex': number;
        /** (read-only) Returns the position of the matching point. */
        readonly 'pos': Position;
        /** (read-only) Returns the type of the matching point. */
        readonly 'type': 'ROUTE_MATCHING_POINT_UNMATCHED' | 'ROUTE_MATCHING_POINT_INTERPOLATED' | 'ROUTE_MATCHING_POINT_MATCHED' | number;
    };
    'massif::RouteMatchingRequest': {
        /** (read-only) Returns the accuracy of the points in the request. */
        readonly 'accuracy': number;
        /** (read-only) Returns the measured points of the request. */
        readonly 'points': Json;
        /** (read-only) Returns the projection of the points in the request. */
        readonly 'projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'projection.bounds': Bounds;
        readonly 'projection.name': string;
    };
    'massif::RouteMatchingResult': {
        /** (read-only) Returns the list with details of the matched edges. */
        readonly 'matchingEdges': Json;
        /** (read-only) Returns the list with details of the matched points. */
        readonly 'matchingPoints': Json;
        /** (read-only) Returns the point list of the result. The list contains all the points from the request snapped to the road network. */
        readonly 'points': Json;
        /** (read-only) Returns the projection of the points in the result. */
        readonly 'projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'projection.bounds': Bounds;
        readonly 'projection.name': string;
        /** (read-only) Returns raw result */
        readonly 'rawResult': string;
    };
    'massif::RoutingInstruction': {
        /** (read-only) Returns the action of the instruction. */
        readonly 'action': 'ROUTING_ACTION_HEAD_ON' | 'ROUTING_ACTION_FINISH' | 'ROUTING_ACTION_NO_TURN' | 'ROUTING_ACTION_GO_STRAIGHT' | 'ROUTING_ACTION_TURN_RIGHT' | 'ROUTING_ACTION_UTURN' | 'ROUTING_ACTION_TURN_LEFT' | 'ROUTING_ACTION_REACH_VIA_LOCATION' | 'ROUTING_ACTION_ENTER_ROUNDABOUT' | 'ROUTING_ACTION_LEAVE_ROUNDABOUT' | 'ROUTING_ACTION_STAY_ON_ROUNDABOUT' | 'ROUTING_ACTION_START_AT_END_OF_STREET' | 'ROUTING_ACTION_ENTER_AGAINST_ALLOWED_DIRECTION' | 'ROUTING_ACTION_LEAVE_AGAINST_ALLOWED_DIRECTION' | 'ROUTING_ACTION_GO_UP' | 'ROUTING_ACTION_GO_DOWN' | 'ROUTING_ACTION_WAIT' | 'ROUTING_ACTION_ENTER_FERRY' | 'ROUTING_ACTION_LEAVE_FERRY' | number;
        /** (read-only) Returns the azimuth of the initial position. */
        readonly 'azimuth': number;
        /** (read-only) Returns the distance to move along the given street. */
        readonly 'distance': number;
        /** (read-only) Returns the geometry tag associated with the instructions. */
        readonly 'geometryTag': Json;
        /** (read-only) Returns the optional instruction description. This info is dependent on the routing engine (can be empty) and may be localized. */
        readonly 'instruction': string;
        /** (read-only) Returns the index of the first geometry point in external point array. */
        readonly 'pointIndex': number;
        /** (read-only) Returns the name of street. */
        readonly 'streetName': string;
        /** (read-only) Returns the time approximate duration of the instruction. */
        readonly 'time': number;
        /** (read-only) Returns the turn angle of the action. */
        readonly 'turnAngle': number;
    };
    'massif::RoutingRequest': {
        /** Returns the custom parameter value of the request. */
        'params': Record<string, Json>;
        /** Returns the custom parameter value of the request. */
        [key: `params.${string}`]: Json;
        /** (read-only) Returns the point list of the request. */
        readonly 'points': Json;
        /** (read-only) Returns the projection of the points in the request. */
        readonly 'projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'projection.bounds': Bounds;
        readonly 'projection.name': string;
    };
    'massif::RoutingResult': {
        /** (read-only) Returns the number of turn-by-turn instructions. */
        readonly 'instructionCount': number;
        /** (read-only) Returns the turn-by-turn instruction list. */
        readonly 'instructions': Json;
        /** (read-only) Returns every turn-by-turn instruction as one JSON array, saving a binding call per field. Keys: `action` (the enum's integer value), `pointIndex`, `streetName`, `instruction`, `turnAngle`, `azimuth`, `distance`, `time`. */
        readonly 'instructionsJSON': string;
        /** (read-only) Returns the number of points in the path. */
        readonly 'pointCount': number;
        /** (read-only) Returns the point list of the result. The list contains all the points the route must pass in correct order. */
        readonly 'points': Json;
        /** (read-only) Returns the projection of the points in the result. */
        readonly 'projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'projection.bounds': Bounds;
        readonly 'projection.name': string;
        /** (read-only) Returns the raw result. */
        readonly 'rawResult': string;
        /** (read-only) Returns the total distance of the path. */
        readonly 'totalDistance': number;
        /** (read-only) Returns the approximate total duration of the path. */
        readonly 'totalTime': number;
    };
    'massif::RoutingService': {
        'profile': string;
    };
    'massif::SGREOfflineRoutingService': {
        'profile': string;
    };
    'massif::ScreenBounds': {
        /** (read-only) Calculates the center screen position of this screen envelope object. */
        readonly 'center': [number, number];
        /** (read-only) Returns the maximum screen position of this screen envelope object. */
        readonly 'max': [number, number];
        /** (read-only) Returns the minimum screen position. */
        readonly 'min': [number, number];
    };
    'massif::ScreenPos': {
        /** (read-only) Returns the x coordinate of this position. */
        readonly 'x': number;
        /** (read-only) Returns the y coordinate of this position. */
        readonly 'y': number;
    };
    'massif::SearchRequest': {
        /** Returns the string based search expression. If empty, then search expression is not used. */
        'filterExpression': string;
        /** Returns the geometry used for proximity search. */
        'geometry': Handle;
        /** (read-only) Returns the minimal bounds for the geometry. */
        readonly 'geometry.bounds': Bounds;
        readonly 'geometry.centerPos': Position;
        /** (read-only) Returns the geometry as a GeoJSON string, in its own coordinates. Here rather than only on Feature because serialising a shape otherwise means constructing a GeoJSONGeometryWriter, which no string-based binding can do. */
        readonly 'geometry.geoJSON': string;
        readonly 'geometry.type': 'GEOMETRY_TYPE_POINT' | 'GEOMETRY_TYPE_LINE' | 'GEOMETRY_TYPE_POLYGON' | 'GEOMETRY_TYPE_MULTIPOINT' | 'GEOMETRY_TYPE_MULTILINE' | 'GEOMETRY_TYPE_MULTIPOLYGON' | 'GEOMETRY_TYPE_COLLECTION' | number;
        /** Returns the projection to use for search geometry. */
        'projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'projection.bounds': Bounds;
        readonly 'projection.name': string;
        /** Returns the regular expression used to search all the fields. If empty, then the regular expression is not used. */
        'regexFilter': string;
        /** Returns the search radius for proximity search (in meters). The default is 0. */
        'searchRadius': number;
    };
    'massif::SkyOptions': {
        /** Returns the tint applied to Rayleigh scattering. */
        'atmosphereColor': number;
        /** Returns the exposure applied to the scattered light. */
        'atmosphereLuminance': number;
        /** Returns the brightness of the sun driving the atmosphere. */
        'atmosphereSunIntensity': number;
        /** Returns whether the shader sky is enabled. */
        'enabled': boolean;
        /** Returns the ground color. */
        'groundColor': number;
        /** Returns the tint applied to Mie scattering. */
        'haloColor': number;
        /** Returns the angular blend width between the horizon color and the sky color. */
        'horizonBlend': number;
        /** Returns the horizon color. */
        'horizonColor': number;
        /** Returns how finely the atmosphere is integrated. */
        'quality': 'SKY_QUALITY_LOW' | 'SKY_QUALITY_MEDIUM' | 'SKY_QUALITY_HIGH' | number;
        /** Returns the custom sky fragment shader source, or an empty string if the built-in shader is used. */
        'shaderSource': string;
        /** Returns the zenith sky color. */
        'skyColor': number;
        /** Returns whether the built-in shader draws a sun disc. */
        'sunDiscEnabled': boolean;
        /** Returns what the sky pass draws. */
        'type': 'SKY_TYPE_GRADIENT' | 'SKY_TYPE_ATMOSPHERE' | number;
    };
    'massif::SolidLayer': {
        /** Returns the bitmap of this layer. */
        'bitmap': Handle;
        /** (read-only) Returns the bytes per pixel parameter of this bitmap. Valid values are 1, 2, 3 and 4. */
        readonly 'bitmap.bytesPerPixel': number;
        /** (read-only) Returns the color format of this bitmap. */
        readonly 'bitmap.colorFormat': 'COLOR_FORMAT_UNSUPPORTED' | 'COLOR_FORMAT_BGRA' | 'COLOR_FORMAT_RGBA_4444' | 'COLOR_FORMAT_RGB_565' | number;
        /** (read-only) Returns the height of the bitmap. */
        readonly 'bitmap.height': number;
        /** (read-only) Returns the width of the bitmap. */
        readonly 'bitmap.width': number;
        /** Returns the bitmap scaling factor. */
        'bitmapScale': number;
        /** Returns the color of this layer. */
        'color': number;
        /** Returns the culling delay of the layer in milliseconds. */
        'cullDelay': number;
        /** Returns a copy of the layer meta data map. The changes you make to this map are NOT reflected in the actual meta data of the layer. */
        'metaData': Record<string, Json>;
        /** Returns a copy of the layer meta data map. The changes you make to this map are NOT reflected in the actual meta data of the layer. */
        [key: `metaData.${string}`]: Json;
        /** Returns the opacity of this layer. */
        'opacity': number;
        /** Returns whether this layer goes through the post-process effect. */
        'postProcessed': boolean;
        /** Returns the layer task priority of this layer. */
        'updatePriority': number;
        /** Returns the visibility of this layer. */
        'visible': boolean;
        /** Returns the visible zoom range of this layer. */
        'visibleZoomRange': [number, number];
    };
    'massif::Style': {
        /** (read-only) Returns the color of the vector element. */
        readonly 'color': number;
    };
    'massif::StyleBuilder': {
        /** Returns the color of the vector element. */
        'color': number;
    };
    'massif::TerrainOptions': {
        /** Returns how long the terrain takes to sink flat. */
        'autoFlattenDuration': number;
        /** Returns the screen parallax below which the terrain renders flat. */
        'autoFlattenParallax': number;
        /** Returns how long the terrain takes to rise back into 3D. */
        'autoFlattenRiseDuration': number;
        /** Returns the tilt at or above which the terrain renders flat. */
        'autoFlattenTilt': number;
        /** Returns the terrain background color. */
        'backgroundColor': number;
        /** Returns the billboard/label terrain occlusion state. */
        'billboardOcclusionEnabled': boolean;
        /** Returns the billboard/label terrain occlusion tolerance. */
        'billboardOcclusionTolerance': number;
        /** Returns whether bridges and tunnels stand on their own chord (3D bridges). */
        'bridges3DEnabled': boolean;
        /** Returns the duration of the camera terrain-following correction animation. */
        'cameraClampDuration': number;
        /** Returns the camera terrain clearance floor: an explicit minimum height the camera is kept above the terrain surface, in meters. */
        'cameraClearance': number;
        /** Returns the share of the camera's altitude that the terrain clearance takes. */
        'cameraClearanceFraction': number;
        /** Returns the clip-space depth bias used when depth-testing draped 2D geometry against the terrain. */
        'depthBias': number;
        /** Returns the drape cache budget in megabytes. */
        'drapeCacheSize': number;
        /** Returns whether polygon fills are draped as a render-to-texture surface. */
        'drapeFillsEnabled': boolean;
        /** Returns whether vt tile lines are also draped (in addition to fills). */
        'drapeLinesEnabled': boolean;
        /** Returns the per-tile drape texture resolution, 0 when it follows the screen. */
        'drapeResolution': number;
        /** Returns how many drape tiles the automatic resolution assumes are cached at once. */
        'drapeWorkingSet': number;
        /** Returns the elevation grid cache budget in megabytes, 0 for the SDK's own rule. */
        'elevationCacheSize': number;
        /** Returns whether elevation tile prefetching is enabled. */
        'elevationPrefetchEnabled': boolean;
        /** Returns the enabled state of the terrain. */
        'enabled': boolean;
        /** Returns the terrain height exaggeration factor. */
        'exaggeration': number;
        /** Returns how far a flattened terrain goes back towards a plain 2D map. */
        'flattenMode': 'TERRAIN_FLATTEN_MODE_RENDER' | 'TERRAIN_FLATTEN_MODE_FULL' | number;
        /** Returns how far the terrain is flattened right now, 0 (full 3D) to 1 (flat). */
        'flattenRatio': number;
        /** Returns whether the map is asked to render flat. This is the 2D/3D state, whether it was set by the app or by auto-flattening; the switch itself is animated, so for a moment after a change the map is still on its way there. */
        'flattened': boolean;
        /** Returns the height the viewpoint is lifted above the ground-following focus, in meters. */
        'focusLift': number;
        /** Returns how many zoom levels below the camera a tile may coarsen to. */
        'maxTileZoomCoarsening': number;
        /** Returns the maximum visible tile zoom offset, relative to the camera zoom level. */
        'maxTileZoomOffset': number;
        /** Returns the maximum tile zoom level the terrain mesh is cut at. */
        'maxZoom': number;
        /** Returns how many terrain surface meshes may be cached. */
        'meshCacheSize': number;
        /** Returns the terrain mesh resolution. */
        'meshResolution': number;
        /** Returns the minimum tile zoom level with 3D terrain. */
        'minZoom': number;
        /** Returns the style layers that are kept out of the terrain drape bake. */
        'noDrapeLayerFilter': string;
        /** Returns the ground distance the surface normals are measured over, in meters. */
        'normalSampleDistance': number;
        /** Returns the downscale factor of the packed depth/normal texture post-process effects read. */
        'postProcessDownscale': number;
        /** Returns whether seamless tile edge handling is enabled. */
        'seamlessTileEdgesEnabled': boolean;
        /** Returns whether the shared ground pass draws the terrain a second time. */
        'sharedGroundEnabled': boolean;
        /** Returns the distance geo-three's terrain LOD subdivides at. */
        'subdivideDistance': number;
        /** Returns the resolution the elevation node field is built at. */
        'surfaceNodeResolution': number;
        /** Returns the custom terrain surface fragment shader source, or an empty string if no shaded surface is drawn. */
        'surfaceShaderSource': string;
        /** (read-only) Returns whether the switch is holding the ground flat while the tiles 3D needs load. */
        readonly 'switching': boolean;
        /** Returns the opacity a label keeps while its anchor is behind 3D content. */
        'textOcclusionOpacity': number;
        /** Returns whether cross-LOD tile edge stitching is enabled. */
        'tileEdgeStitchingEnabled': boolean;
        /** Returns the minimum view distance, in meters. */
        'viewDistance': number;
        /** Returns the factor applied to the view distance. */
        'viewDistanceFactor': number;
        /** Returns the maximum view distance, in meters. */
        'viewDistanceMax': number;
    };
    'massif::TerrariumElevationDataDecoder': {
    };
    'massif::Text': {
        /** Returns the base billboard this billboard is attached to. */
        'baseBillboard': Handle;
        /** Returns the base billboard this billboard is attached to. */
        'baseBillboard.baseBillboard': Handle;
        /** (read-only) Returns the bounds of this billboard or the base billboard, if there is one. */
        readonly 'baseBillboard.bounds': Bounds;
        /** Returns the geometry object that defines the location of this billboard. */
        'baseBillboard.geometry': Handle;
        /** (read-only) Returns the minimal bounds for the geometry. */
        readonly 'baseBillboard.geometry.bounds': Bounds;
        readonly 'baseBillboard.geometry.centerPos': Position;
        /** (read-only) Returns the geometry as a GeoJSON string, in its own coordinates. Here rather than only on Feature because serialising a shape otherwise means constructing a GeoJSONGeometryWriter, which no string-based binding can do. */
        readonly 'baseBillboard.geometry.geoJSON': string;
        readonly 'baseBillboard.geometry.type': 'GEOMETRY_TYPE_POINT' | 'GEOMETRY_TYPE_LINE' | 'GEOMETRY_TYPE_POLYGON' | 'GEOMETRY_TYPE_MULTIPOINT' | 'GEOMETRY_TYPE_MULTILINE' | 'GEOMETRY_TYPE_MULTIPOLYGON' | 'GEOMETRY_TYPE_COLLECTION' | number;
        /** Returns the internal id of this vector element. */
        'baseBillboard.id': number;
        /** Returns a copy of the vector element meta data map. The changes you make to this map are NOT reflected in the actual meta data of the element. */
        'baseBillboard.metaData': Record<string, Json>;
        /** Returns a copy of the vector element meta data map. The changes you make to this map are NOT reflected in the actual meta data of the element. */
        [key: `baseBillboard.metaData.${string}`]: Json;
        /** (read-only) Returns the location of the root billboard: getGeometry() if this billboard has a location, otherwise the location found by following the chain of base billboards to its root. */
        readonly 'baseBillboard.rootGeometry': Handle;
        /** (read-only) Returns the minimal bounds for the geometry. */
        readonly 'baseBillboard.rootGeometry.bounds': Bounds;
        readonly 'baseBillboard.rootGeometry.centerPos': Position;
        /** (read-only) Returns the geometry as a GeoJSON string, in its own coordinates. Here rather than only on Feature because serialising a shape otherwise means constructing a GeoJSONGeometryWriter, which no string-based binding can do. */
        readonly 'baseBillboard.rootGeometry.geoJSON': string;
        readonly 'baseBillboard.rootGeometry.type': 'GEOMETRY_TYPE_POINT' | 'GEOMETRY_TYPE_LINE' | 'GEOMETRY_TYPE_POLYGON' | 'GEOMETRY_TYPE_MULTIPOINT' | 'GEOMETRY_TYPE_MULTILINE' | 'GEOMETRY_TYPE_MULTIPOLYGON' | 'GEOMETRY_TYPE_COLLECTION' | number;
        /** Returns the rotation angle of this billboard. */
        'baseBillboard.rotation': number;
        /** Returns the state of the visibility flag of this vector element. */
        'baseBillboard.visible': boolean;
        /** (read-only) Returns the bounds of this billboard or the base billboard, if there is one. */
        readonly 'bounds': Bounds;
        /** Returns the geometry object that defines the location of this billboard. */
        'geometry': Handle;
        /** (read-only) Returns the minimal bounds for the geometry. */
        readonly 'geometry.bounds': Bounds;
        readonly 'geometry.centerPos': Position;
        /** (read-only) Returns the geometry as a GeoJSON string, in its own coordinates. Here rather than only on Feature because serialising a shape otherwise means constructing a GeoJSONGeometryWriter, which no string-based binding can do. */
        readonly 'geometry.geoJSON': string;
        readonly 'geometry.type': 'GEOMETRY_TYPE_POINT' | 'GEOMETRY_TYPE_LINE' | 'GEOMETRY_TYPE_POLYGON' | 'GEOMETRY_TYPE_MULTIPOINT' | 'GEOMETRY_TYPE_MULTILINE' | 'GEOMETRY_TYPE_MULTIPOLYGON' | 'GEOMETRY_TYPE_COLLECTION' | number;
        /** Returns the internal id of this vector element. */
        'id': number;
        /** Returns a copy of the vector element meta data map. The changes you make to this map are NOT reflected in the actual meta data of the element. */
        'metaData': Record<string, Json>;
        /** Returns a copy of the vector element meta data map. The changes you make to this map are NOT reflected in the actual meta data of the element. */
        [key: `metaData.${string}`]: Json;
        /** (read-only) Returns the location of the root billboard: getGeometry() if this billboard has a location, otherwise the location found by following the chain of base billboards to its root. */
        readonly 'rootGeometry': Handle;
        /** (read-only) Returns the minimal bounds for the geometry. */
        readonly 'rootGeometry.bounds': Bounds;
        readonly 'rootGeometry.centerPos': Position;
        /** (read-only) Returns the geometry as a GeoJSON string, in its own coordinates. Here rather than only on Feature because serialising a shape otherwise means constructing a GeoJSONGeometryWriter, which no string-based binding can do. */
        readonly 'rootGeometry.geoJSON': string;
        readonly 'rootGeometry.type': 'GEOMETRY_TYPE_POINT' | 'GEOMETRY_TYPE_LINE' | 'GEOMETRY_TYPE_POLYGON' | 'GEOMETRY_TYPE_MULTIPOINT' | 'GEOMETRY_TYPE_MULTILINE' | 'GEOMETRY_TYPE_MULTIPOLYGON' | 'GEOMETRY_TYPE_COLLECTION' | number;
        /** Returns the rotation angle of this billboard. */
        'rotation': number;
        /** Returns the style of this text label. */
        'style': Handle;
        /** (read-only) Returns the horizontal anchor point of the label. */
        readonly 'style.anchorPointX': number;
        /** (read-only) Returns the vertical anchor point of the label. */
        readonly 'style.anchorPointY': number;
        /** (read-only) Returns the animation style of the billboard. */
        readonly 'style.animationStyle': Handle;
        /** (read-only) Returns the fade animation type. */
        readonly 'style.animationStyle.fadeAnimationType': 'ANIMATION_TYPE_NONE' | 'ANIMATION_TYPE_STEP' | 'ANIMATION_TYPE_LINEAR' | 'ANIMATION_TYPE_SMOOTHSTEP' | 'ANIMATION_TYPE_SPRING' | number;
        /** (read-only) Returns the phase-in duration of the animation. */
        readonly 'style.animationStyle.phaseInDuration': number;
        /** (read-only) Returns the phase-out duration of the animation. */
        readonly 'style.animationStyle.phaseOutDuration': number;
        /** (read-only) Returns the relative speed of the animation. */
        readonly 'style.animationStyle.relativeSpeed': number;
        /** (read-only) Returns the size-related animation type. */
        readonly 'style.animationStyle.sizeAnimationType': 'ANIMATION_TYPE_NONE' | 'ANIMATION_TYPE_STEP' | 'ANIMATION_TYPE_LINEAR' | 'ANIMATION_TYPE_SMOOTHSTEP' | 'ANIMATION_TYPE_SPRING' | number;
        /** (read-only) Returns the horizontal attaching anchor point of the billoard. */
        readonly 'style.attachAnchorPointX': number;
        /** (read-only) Returns the vertical attaching anchor point of the billoard. */
        readonly 'style.attachAnchorPointY': number;
        /** (read-only) Returns the background color of the text. */
        readonly 'style.backgroundColor': number;
        /** (read-only) Returns the color of the border. */
        readonly 'style.borderColor': number;
        /** (read-only) Returns the width of the border. */
        readonly 'style.borderWidth': number;
        /** (read-only) Returns the state of the 'break lines' flag. */
        readonly 'style.breakLines': boolean;
        /** (read-only) Returns the state of the causes overlap flag. */
        readonly 'style.causesOverlap': boolean;
        /** (read-only) Returns the color of the vector element. */
        readonly 'style.color': number;
        /** (read-only) Returns the state of the flippable flag. */
        readonly 'style.flippable': boolean;
        /** (read-only) Returns the font's color. */
        readonly 'style.fontColor': number;
        /** (read-only) Returns the font's name. */
        readonly 'style.fontName': string;
        /** (read-only) Returns the font's size. */
        readonly 'style.fontSize': number;
        /** (read-only) Returns the state of the allow overlap flag. */
        readonly 'style.hideIfOverlapped': boolean;
        /** (read-only) Returns the horizontal offset of the billboard. */
        readonly 'style.horizontalOffset': number;
        /** (read-only) Returns the orientation mode of the label. */
        readonly 'style.orientationMode': 'BILLBOARD_ORIENTATION_FACE_CAMERA' | 'BILLBOARD_ORIENTATION_FACE_CAMERA_GROUND' | 'BILLBOARD_ORIENTATION_GROUND' | number;
        /** (read-only) Returns the placement priority of the billboard. */
        readonly 'style.placementPriority': number;
        /** (read-only) Returns the relative rendering scale of the label. */
        readonly 'style.renderScale': number;
        /** (read-only) Returns the state of the scale with DPI flag. */
        readonly 'style.scaleWithDPI': boolean;
        /** (read-only) Returns the scaling mode of the label. */
        readonly 'style.scalingMode': 'BILLBOARD_SCALING_WORLD_SIZE' | 'BILLBOARD_SCALING_SCREEN_SIZE' | 'BILLBOARD_SCALING_CONST_SCREEN_SIZE' | number;
        /** (read-only) Returns the color of the stroke. */
        readonly 'style.strokeColor': number;
        /** (read-only) Returns the width of the stroke. */
        readonly 'style.strokeWidth': number;
        /** (read-only) Returns the text field variable to use. */
        readonly 'style.textField': string;
        /** (read-only) Returns the margins for the text. */
        readonly 'style.textMargins': Json;
        /** (read-only) Returns the vertical offset of the billboard. */
        readonly 'style.verticalOffset': number;
        /** Returns the display text. */
        'title': string;
        /** Returns the state of the visibility flag of this vector element. */
        'visible': boolean;
    };
    'massif::TextMargins': {
        readonly 'bottom': number;
        readonly 'left': number;
        readonly 'right': number;
        readonly 'top': number;
    };
    'massif::TextStyle': {
        /** (read-only) Returns the horizontal anchor point of the label. */
        readonly 'anchorPointX': number;
        /** (read-only) Returns the vertical anchor point of the label. */
        readonly 'anchorPointY': number;
        /** (read-only) Returns the animation style of the billboard. */
        readonly 'animationStyle': Handle;
        /** (read-only) Returns the fade animation type. */
        readonly 'animationStyle.fadeAnimationType': 'ANIMATION_TYPE_NONE' | 'ANIMATION_TYPE_STEP' | 'ANIMATION_TYPE_LINEAR' | 'ANIMATION_TYPE_SMOOTHSTEP' | 'ANIMATION_TYPE_SPRING' | number;
        /** (read-only) Returns the phase-in duration of the animation. */
        readonly 'animationStyle.phaseInDuration': number;
        /** (read-only) Returns the phase-out duration of the animation. */
        readonly 'animationStyle.phaseOutDuration': number;
        /** (read-only) Returns the relative speed of the animation. */
        readonly 'animationStyle.relativeSpeed': number;
        /** (read-only) Returns the size-related animation type. */
        readonly 'animationStyle.sizeAnimationType': 'ANIMATION_TYPE_NONE' | 'ANIMATION_TYPE_STEP' | 'ANIMATION_TYPE_LINEAR' | 'ANIMATION_TYPE_SMOOTHSTEP' | 'ANIMATION_TYPE_SPRING' | number;
        /** (read-only) Returns the horizontal attaching anchor point of the billoard. */
        readonly 'attachAnchorPointX': number;
        /** (read-only) Returns the vertical attaching anchor point of the billoard. */
        readonly 'attachAnchorPointY': number;
        /** (read-only) Returns the background color of the text. */
        readonly 'backgroundColor': number;
        /** (read-only) Returns the color of the border. */
        readonly 'borderColor': number;
        /** (read-only) Returns the width of the border. */
        readonly 'borderWidth': number;
        /** (read-only) Returns the state of the 'break lines' flag. */
        readonly 'breakLines': boolean;
        /** (read-only) Returns the state of the causes overlap flag. */
        readonly 'causesOverlap': boolean;
        /** (read-only) Returns the color of the vector element. */
        readonly 'color': number;
        /** (read-only) Returns the state of the flippable flag. */
        readonly 'flippable': boolean;
        /** (read-only) Returns the font's color. */
        readonly 'fontColor': number;
        /** (read-only) Returns the font's name. */
        readonly 'fontName': string;
        /** (read-only) Returns the font's size. */
        readonly 'fontSize': number;
        /** (read-only) Returns the state of the allow overlap flag. */
        readonly 'hideIfOverlapped': boolean;
        /** (read-only) Returns the horizontal offset of the billboard. */
        readonly 'horizontalOffset': number;
        /** (read-only) Returns the orientation mode of the label. */
        readonly 'orientationMode': 'BILLBOARD_ORIENTATION_FACE_CAMERA' | 'BILLBOARD_ORIENTATION_FACE_CAMERA_GROUND' | 'BILLBOARD_ORIENTATION_GROUND' | number;
        /** (read-only) Returns the placement priority of the billboard. */
        readonly 'placementPriority': number;
        /** (read-only) Returns the relative rendering scale of the label. */
        readonly 'renderScale': number;
        /** (read-only) Returns the state of the scale with DPI flag. */
        readonly 'scaleWithDPI': boolean;
        /** (read-only) Returns the scaling mode of the label. */
        readonly 'scalingMode': 'BILLBOARD_SCALING_WORLD_SIZE' | 'BILLBOARD_SCALING_SCREEN_SIZE' | 'BILLBOARD_SCALING_CONST_SCREEN_SIZE' | number;
        /** (read-only) Returns the color of the stroke. */
        readonly 'strokeColor': number;
        /** (read-only) Returns the width of the stroke. */
        readonly 'strokeWidth': number;
        /** (read-only) Returns the text field variable to use. */
        readonly 'textField': string;
        /** (read-only) Returns the margins for the text. */
        readonly 'textMargins': Json;
        /** (read-only) Returns the vertical offset of the billboard. */
        readonly 'verticalOffset': number;
    };
    'massif::TextStyleBuilder': {
        /** Returns the horizontal anchor point of the label. */
        'anchorPointX': number;
        /** Returns the vertical anchor point of the label. */
        'anchorPointY': number;
        /** Returns the animation style of the billboard. */
        'animationStyle': Handle;
        /** (read-only) Returns the fade animation type. */
        readonly 'animationStyle.fadeAnimationType': 'ANIMATION_TYPE_NONE' | 'ANIMATION_TYPE_STEP' | 'ANIMATION_TYPE_LINEAR' | 'ANIMATION_TYPE_SMOOTHSTEP' | 'ANIMATION_TYPE_SPRING' | number;
        /** (read-only) Returns the phase-in duration of the animation. */
        readonly 'animationStyle.phaseInDuration': number;
        /** (read-only) Returns the phase-out duration of the animation. */
        readonly 'animationStyle.phaseOutDuration': number;
        /** (read-only) Returns the relative speed of the animation. */
        readonly 'animationStyle.relativeSpeed': number;
        /** (read-only) Returns the size-related animation type. */
        readonly 'animationStyle.sizeAnimationType': 'ANIMATION_TYPE_NONE' | 'ANIMATION_TYPE_STEP' | 'ANIMATION_TYPE_LINEAR' | 'ANIMATION_TYPE_SMOOTHSTEP' | 'ANIMATION_TYPE_SPRING' | number;
        /** Returns the horizontal attaching anchor point of the billboard. */
        'attachAnchorPointX': number;
        /** Returns the vertical attaching anchor point of the billboard. */
        'attachAnchorPointY': number;
        /** Returns the background color for the text label. */
        'backgroundColor': number;
        /** Returns the border color for the text label. */
        'borderColor': number;
        /** Returns the border width for the text label. */
        'borderWidth': number;
        /** Returns the state of the 'break lines' flag. */
        'breakLines': boolean;
        /** Returns the state of the causes overlap flag. */
        'causesOverlap': boolean;
        /** Returns the color of the vector element. */
        'color': number;
        /** Returns the state of the flippable flag. */
        'flippable': boolean;
        /** Returns the font name for the text label. */
        'fontName': string;
        /** Returns the font size for the text label. */
        'fontSize': number;
        /** Returns the state of the allow overlap flag. */
        'hideIfOverlapped': boolean;
        /** Returns the horizontal offset of the billboard. */
        'horizontalOffset': number;
        /** Returns the orientation mode of the label. */
        'orientationMode': 'BILLBOARD_ORIENTATION_FACE_CAMERA' | 'BILLBOARD_ORIENTATION_FACE_CAMERA_GROUND' | 'BILLBOARD_ORIENTATION_GROUND' | number;
        /** Returns the placement priority of the billboard. */
        'placementPriority': number;
        /** Returns the relative rendering scale for the label. */
        'renderScale': number;
        /** Returns the state of the scale with DPI flag. */
        'scaleWithDPI': boolean;
        /** Returns the scaling mode of the label. */
        'scalingMode': 'BILLBOARD_SCALING_WORLD_SIZE' | 'BILLBOARD_SCALING_SCREEN_SIZE' | 'BILLBOARD_SCALING_CONST_SCREEN_SIZE' | number;
        /** Returns the stroke color for the text label. */
        'strokeColor': number;
        /** Returns the stroke width for the text label. */
        'strokeWidth': number;
        /** Returns the text field variable. If not empty, this variable is used to read actual text string from object meta info. */
        'textField': string;
        /** Returns the margins for the text. */
        'textMargins': Json;
        /** Returns the vertical offset of the billboard. */
        'verticalOffset': number;
    };
    'massif::TileData': {
        /** (read-only) Returns tile data as binary data. */
        readonly 'data': Handle;
        /** (read-only) Returns the size of the data */
        readonly 'data.size': number;
        /** (read-only) The pixel height, or 0 when the data is an encoded file. */
        readonly 'height': number;
        /** Returns the maximum age of the tile data, tile data will expire after that point. */
        'maxAge': number;
        /** (read-only) Returns true when getData() holds raw RGBA8 pixels rather than an encoded file. A consumer that turns tiles into bitmaps has to check this before decoding. */
        readonly 'rawPixels': boolean;
        /** Returns true if the tile should be replaced with parent tile. */
        'replaceWithParent': boolean;
        /** (read-only) The pixel width, or 0 when the data is an encoded file. */
        readonly 'width': number;
    };
    'massif::TileDataSource': {
        /** (read-only) Returns the extent of the tiles in this data source. The bounds are in coordinate system of the projection of the data source. */
        readonly 'dataExtent': Bounds;
        /** Gets the current maximum overzoom level for this datasource. Over it the datasource will not be "drawn" */
        'maxOverzoomLevel': number;
        /** (read-only) Returns the maximum zoom level supported by this data source. */
        readonly 'maxZoom': number;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        'metaData': Record<string, Json>;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        [key: `metaData.${string}`]: Json;
        /** (read-only) Returns the minimum zoom level supported by this data source. */
        readonly 'minZoom': number;
        /** (read-only) Returns the projection of this tile source. */
        readonly 'projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'projection.bounds': Bounds;
        readonly 'projection.name': string;
    };
    'massif::TileDecoderListener': {
    };
    'massif::TileDownloadInfo': {
        /** (read-only) Returns the progress of the download. */
        readonly 'progress': number;
        /** (read-only) Returns the tile the event concerns, which is only meaningful for a failure. */
        readonly 'tile': Tile;
        /** (read-only) Returns the number of tiles the download will fetch. */
        readonly 'tileCount': number;
    };
    'massif::TileDownloadListener': {
    };
    'massif::TileInfo': {
    };
    'massif::TileLayer': {
        /** Returns the tile data source of the associated UTF grid. By default this is null. */
        'UTFGridDataSource': Handle;
        /** (read-only) Returns the extent of the tiles in this data source. The bounds are in coordinate system of the projection of the data source. */
        readonly 'UTFGridDataSource.dataExtent': Bounds;
        /** Gets the current maximum overzoom level for this datasource. Over it the datasource will not be "drawn" */
        'UTFGridDataSource.maxOverzoomLevel': number;
        /** (read-only) Returns the maximum zoom level supported by this data source. */
        readonly 'UTFGridDataSource.maxZoom': number;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        'UTFGridDataSource.metaData': Record<string, Json>;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        [key: `UTFGridDataSource.metaData.${string}`]: Json;
        /** (read-only) Returns the minimum zoom level supported by this data source. */
        readonly 'UTFGridDataSource.minZoom': number;
        /** (read-only) Returns the projection of this tile source. */
        readonly 'UTFGridDataSource.projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'UTFGridDataSource.projection.bounds': Bounds;
        readonly 'UTFGridDataSource.projection.name': string;
        /** Returns the UTF grid event listener. */
        'UTFGridEventListener': Handle;
        /** Returns the culling delay of the layer in milliseconds. */
        'cullDelay': number;
        /** (read-only) Returns the data source assigned to this layer. */
        readonly 'dataSource': Handle;
        /** (read-only) Returns the extent of the tiles in this data source. The bounds are in coordinate system of the projection of the data source. */
        readonly 'dataSource.dataExtent': Bounds;
        /** Gets the current maximum overzoom level for this datasource. Over it the datasource will not be "drawn" */
        'dataSource.maxOverzoomLevel': number;
        /** (read-only) Returns the maximum zoom level supported by this data source. */
        readonly 'dataSource.maxZoom': number;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        'dataSource.metaData': Record<string, Json>;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        [key: `dataSource.metaData.${string}`]: Json;
        /** (read-only) Returns the minimum zoom level supported by this data source. */
        readonly 'dataSource.minZoom': number;
        /** (read-only) Returns the projection of this tile source. */
        readonly 'dataSource.projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'dataSource.projection.bounds': Bounds;
        readonly 'dataSource.projection.name': string;
        /** Returns the current frame number. */
        'frameNr': number;
        /** Gets the current maximum overzoom level for this layer. */
        'maxOverzoomLevel': number;
        /** Gets the current maximum underzoom level for this layer. */
        'maxUnderzoomLevel': number;
        /** Returns a copy of the layer meta data map. The changes you make to this map are NOT reflected in the actual meta data of the layer. */
        'metaData': Record<string, Json>;
        /** Returns a copy of the layer meta data map. The changes you make to this map are NOT reflected in the actual meta data of the layer. */
        [key: `metaData.${string}`]: Json;
        /** Returns the opacity of this layer. */
        'opacity': number;
        /** Returns whether this layer goes through the post-process effect. */
        'postProcessed': boolean;
        /** Returns the state of the preloading flag of this layer. */
        'preloading': boolean;
        readonly 'preloadingTileCount': number;
        /** (read-only) Returns the projection this layer's data is in, which is its data source's. */
        readonly 'projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'projection.bounds': Bounds;
        readonly 'projection.name': string;
        /** (read-only) Returns the data source assigned to this layer. */
        readonly 'source': Handle;
        /** (read-only) Returns the extent of the tiles in this data source. The bounds are in coordinate system of the projection of the data source. */
        readonly 'source.dataExtent': Bounds;
        /** Gets the current maximum overzoom level for this datasource. Over it the datasource will not be "drawn" */
        'source.maxOverzoomLevel': number;
        /** (read-only) Returns the maximum zoom level supported by this data source. */
        readonly 'source.maxZoom': number;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        'source.metaData': Record<string, Json>;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        [key: `source.metaData.${string}`]: Json;
        /** (read-only) Returns the minimum zoom level supported by this data source. */
        readonly 'source.minZoom': number;
        /** (read-only) Returns the projection of this tile source. */
        readonly 'source.projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'source.projection.bounds': Bounds;
        readonly 'source.projection.name': string;
        /** Returns the state of the synchronized refresh flag. */
        'synchronizedRefresh': boolean;
        /** Returns the tile load listener. */
        'tileLoadListener': Handle;
        /** Returns the current tile substitution policy. */
        'tileSubstitutionPolicy': 'TILE_SUBSTITUTION_POLICY_ALL' | 'TILE_SUBSTITUTION_POLICY_VISIBLE' | 'TILE_SUBSTITUTION_POLICY_NONE' | number;
        /** Returns the layer task priority of this layer. */
        'updatePriority': number;
        /** Returns the visibility of this layer. */
        'visible': boolean;
        /** (read-only) How many tiles the last cull put on screen. A diagnostic: it is what the tile LOD numbers actually cost. */
        readonly 'visibleTileCount': number;
        /** Returns the visible zoom range of this layer. */
        'visibleZoomRange': [number, number];
        /** Gets the current zoom level bias for this layer. */
        'zoomLevelBias': number;
    };
    'massif::TileLoadListener': {
    };
    'massif::TileUtils': {
    };
    'massif::TomTomOnlineGeocodingService': {
        'autocomplete': boolean;
        /** Returns the custom backend service URL. */
        'customServiceURL': string;
        'language': string;
        'maxResults': number;
    };
    'massif::TomTomOnlineReverseGeocodingService': {
        /** Returns the custom backend service URL. */
        'customServiceURL': string;
        'language': string;
    };
    'massif::TorqueTileDecoder': {
        /** (read-only) Returns the animation duration, in seconds. */
        readonly 'animationDuration': number;
        /** (read-only) Returns the frame count defined in the Torque style. */
        readonly 'frameCount': number;
        readonly 'maxZoom': number;
        readonly 'minZoom': number;
        /** (read-only) Returns the tile resolution, in pixels. */
        readonly 'resolution': number;
        /** Returns the current style set used by the decoder. */
        'styleSet': Handle;
        /** (read-only) Returns the style asset package. */
        readonly 'styleSet.assetPackage': Handle;
        readonly 'styleSet.assetPackage.assetNames': string[];
        /** (read-only) Returns the CartoCSS string used for the style. */
        readonly 'styleSet.cartoCSS': string;
    };
    'massif::TorqueTileLayer': {
        /** Returns the tile data source of the associated UTF grid. By default this is null. */
        'UTFGridDataSource': Handle;
        /** (read-only) Returns the extent of the tiles in this data source. The bounds are in coordinate system of the projection of the data source. */
        readonly 'UTFGridDataSource.dataExtent': Bounds;
        /** Gets the current maximum overzoom level for this datasource. Over it the datasource will not be "drawn" */
        'UTFGridDataSource.maxOverzoomLevel': number;
        /** (read-only) Returns the maximum zoom level supported by this data source. */
        readonly 'UTFGridDataSource.maxZoom': number;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        'UTFGridDataSource.metaData': Record<string, Json>;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        [key: `UTFGridDataSource.metaData.${string}`]: Json;
        /** (read-only) Returns the minimum zoom level supported by this data source. */
        readonly 'UTFGridDataSource.minZoom': number;
        /** (read-only) Returns the projection of this tile source. */
        readonly 'UTFGridDataSource.projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'UTFGridDataSource.projection.bounds': Bounds;
        readonly 'UTFGridDataSource.projection.name': string;
        /** Returns the UTF grid event listener. */
        'UTFGridEventListener': Handle;
        /** Returns the current display order of the buildings. LAST draws over flat labels too: a label that must clear a building is a billboard one, whose pass runs after the buildings. */
        'buildingRenderOrder': 'VECTOR_TILE_RENDER_ORDER_HIDDEN' | 'VECTOR_TILE_RENDER_ORDER_LAYER' | 'VECTOR_TILE_RENDER_ORDER_LAST' | number;
        /** Returns the click handler layer filter. The filter is given as ECMA regular expression that is applied to qualified layer names. */
        'clickHandlerLayerFilter': string;
        /** Returns the click radius of vector tile features. Units are screen density independent pixels (DP or DIP). */
        'clickRadius': number;
        /** Returns the culling delay of the layer in milliseconds. */
        'cullDelay': number;
        /** (read-only) Returns the data source assigned to this layer. */
        readonly 'dataSource': Handle;
        /** (read-only) Returns the extent of the tiles in this data source. The bounds are in coordinate system of the projection of the data source. */
        readonly 'dataSource.dataExtent': Bounds;
        /** Gets the current maximum overzoom level for this datasource. Over it the datasource will not be "drawn" */
        'dataSource.maxOverzoomLevel': number;
        /** (read-only) Returns the maximum zoom level supported by this data source. */
        readonly 'dataSource.maxZoom': number;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        'dataSource.metaData': Record<string, Json>;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        [key: `dataSource.metaData.${string}`]: Json;
        /** (read-only) Returns the minimum zoom level supported by this data source. */
        readonly 'dataSource.minZoom': number;
        /** (read-only) Returns the projection of this tile source. */
        readonly 'dataSource.projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'dataSource.projection.bounds': Bounds;
        readonly 'dataSource.projection.name': string;
        /** Returns the current frame number. */
        'frameNr': number;
        /** Returns the label blending speed, in full fades per second. */
        'labelBlendingSpeed': number;
        /** Returns how much of the perspective divide a label keeps as it recedes from the camera. */
        'labelPerspectiveScaling': number;
        /** Returns the current display order of the labels. */
        'labelRenderOrder': 'VECTOR_TILE_RENDER_ORDER_HIDDEN' | 'VECTOR_TILE_RENDER_ORDER_LAYER' | 'VECTOR_TILE_RENDER_ORDER_LAST' | number;
        /** Returns the current relative layer blending speed. */
        'layerBlendingSpeed': number;
        /** Gets the current maximum overzoom level for this layer. */
        'maxOverzoomLevel': number;
        /** Gets the current maximum underzoom level for this layer. */
        'maxUnderzoomLevel': number;
        /** Returns a copy of the layer meta data map. The changes you make to this map are NOT reflected in the actual meta data of the layer. */
        'metaData': Record<string, Json>;
        /** Returns a copy of the layer meta data map. The changes you make to this map are NOT reflected in the actual meta data of the layer. */
        [key: `metaData.${string}`]: Json;
        /** Returns the opacity of this layer. */
        'opacity': number;
        /** Returns whether this layer goes through the post-process effect. */
        'postProcessed': boolean;
        /** Returns the state of the preloading flag of this layer. */
        'preloading': boolean;
        readonly 'preloadingTileCount': number;
        /** (read-only) Returns the projection this layer's data is in, which is its data source's. */
        readonly 'projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'projection.bounds': Bounds;
        readonly 'projection.name': string;
        /** Returns the renderer layer filter. The filter is given as ECMA regular expression that is applied to qualified layer names. */
        'rendererLayerFilter': string;
        /** (read-only) Returns the data source assigned to this layer. */
        readonly 'source': Handle;
        /** (read-only) Returns the extent of the tiles in this data source. The bounds are in coordinate system of the projection of the data source. */
        readonly 'source.dataExtent': Bounds;
        /** Gets the current maximum overzoom level for this datasource. Over it the datasource will not be "drawn" */
        'source.maxOverzoomLevel': number;
        /** (read-only) Returns the maximum zoom level supported by this data source. */
        readonly 'source.maxZoom': number;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        'source.metaData': Record<string, Json>;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        [key: `source.metaData.${string}`]: Json;
        /** (read-only) Returns the minimum zoom level supported by this data source. */
        readonly 'source.minZoom': number;
        /** (read-only) Returns the projection of this tile source. */
        readonly 'source.projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'source.projection.bounds': Bounds;
        readonly 'source.projection.name': string;
        /** (read-only) Returns the tile decoder assigned to this layer. */
        readonly 'style': Handle;
        readonly 'style.maxZoom': number;
        readonly 'style.minZoom': number;
        /** Returns the state of the synchronized refresh flag. */
        'synchronizedRefresh': boolean;
        /** Returns the tile cache capacity. */
        'tileCacheCapacity': number;
        /** (read-only) Returns the tile decoder assigned to this layer. */
        readonly 'tileDecoder': Handle;
        readonly 'tileDecoder.maxZoom': number;
        readonly 'tileDecoder.minZoom': number;
        /** Returns the tile load listener. */
        'tileLoadListener': Handle;
        /** Returns the current tile substitution policy. */
        'tileSubstitutionPolicy': 'TILE_SUBSTITUTION_POLICY_ALL' | 'TILE_SUBSTITUTION_POLICY_VISIBLE' | 'TILE_SUBSTITUTION_POLICY_NONE' | number;
        /** Returns the layer task priority of this layer. */
        'updatePriority': number;
        /** Returns the vector tile event listener. */
        'vectorTileEventListener': Handle;
        /** Returns the visibility of this layer. */
        'visible': boolean;
        /** (read-only) How many tiles the last cull put on screen. A diagnostic: it is what the tile LOD numbers actually cost. */
        readonly 'visibleTileCount': number;
        /** Returns the visible zoom range of this layer. */
        'visibleZoomRange': [number, number];
        /** Gets the current zoom level bias for this layer. */
        'zoomLevelBias': number;
    };
    'massif::TouchHandlerListener': {
    };
    'massif::UTFGridClickInfo': {
        /** (read-only) Returns the click info. */
        readonly 'clickInfo': ClickInfo;
        /** (read-only) Returns the click position. */
        readonly 'clickPos': Position;
        /** (read-only) Returns the click type. */
        readonly 'clickType': 'CLICK_TYPE_SINGLE' | 'CLICK_TYPE_LONG' | 'CLICK_TYPE_DOUBLE' | 'CLICK_TYPE_DUAL' | number;
        /** (read-only) Returns the info tag of the clicked element. */
        readonly 'elementInfo': Json;
        /** (read-only) Returns the clicked layer. */
        readonly 'layer': Handle;
        /** Returns the culling delay of the layer in milliseconds. */
        'layer.cullDelay': number;
        /** Returns a copy of the layer meta data map. The changes you make to this map are NOT reflected in the actual meta data of the layer. */
        'layer.metaData': Record<string, Json>;
        /** Returns a copy of the layer meta data map. The changes you make to this map are NOT reflected in the actual meta data of the layer. */
        [key: `layer.metaData.${string}`]: Json;
        /** Returns the opacity of this layer. */
        'layer.opacity': number;
        /** Returns whether this layer goes through the post-process effect. */
        'layer.postProcessed': boolean;
        /** Returns the layer task priority of this layer. */
        'layer.updatePriority': number;
        /** Returns the visibility of this layer. */
        'layer.visible': boolean;
        /** Returns the visible zoom range of this layer. */
        'layer.visibleZoomRange': [number, number];
    };
    'massif::UTFGridEventListener': {
    };
    'massif::UiDispatcher': {
    };
    'massif::ValhallaOfflineRoutingService': {
        'profile': string;
    };
    'massif::ValhallaOnlineRoutingService': {
        /** Returns the current set of HTTP headers used. Initially this set is empty and can be changed with setHTTPHeaders. */
        'HTTPHeaders': Record<string, string>;
        /** Returns the current set of HTTP headers used. Initially this set is empty and can be changed with setHTTPHeaders. */
        [key: `HTTPHeaders.${string}`]: string;
        /** Returns the custom backend service URL. */
        'customServiceURL': string;
        'profile': string;
        /** Returns the current timeout value. */
        'timeout': number;
    };
    'massif::Variant': {
        /** (read-only) Returns the number of elements in the array. */
        readonly 'arraySize': number;
        /** (read-only) Returns the boolean value of this variant. */
        readonly 'bool': boolean;
        /** (read-only) Returns the floating point value of this variant. */
        readonly 'double': number;
        /** (read-only) Returns the integer value of this variant. */
        readonly 'long': number;
        /** (read-only) Returns all the keys in the object. */
        readonly 'objectKeys': string[];
        /** (read-only) Returns the string value of this variant. */
        readonly 'string': string;
        /** (read-only) Returns the type of this variant. */
        readonly 'type': 'VARIANT_TYPE_NULL' | 'VARIANT_TYPE_STRING' | 'VARIANT_TYPE_BOOL' | 'VARIANT_TYPE_INTEGER' | 'VARIANT_TYPE_DOUBLE' | 'VARIANT_TYPE_ARRAY' | 'VARIANT_TYPE_OBJECT' | number;
    };
    'massif::VariantArrayBuilder': {
    };
    'massif::VariantObjectBuilder': {
    };
    'massif::VectorData': {
        /** (read-only) Returns the list of vector elements. */
        readonly 'elements': Json;
    };
    'massif::VectorDataSource': {
        /** (read-only) Returns the extent of the data of this data source. The bounds are in coordinate system of the projection of the data source. */
        readonly 'dataExtent': Bounds;
        /** (read-only) Returns the projection used by this data source. */
        readonly 'projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'projection.bounds': Bounds;
        readonly 'projection.name': string;
    };
    'massif::VectorEditEventListener': {
    };
    'massif::VectorElement': {
        /** (read-only) Returns the bounds of this vector element. */
        readonly 'bounds': Bounds;
        /** (read-only) Returns the geometry object that defines the location of this vector element. */
        readonly 'geometry': Handle;
        /** (read-only) Returns the minimal bounds for the geometry. */
        readonly 'geometry.bounds': Bounds;
        readonly 'geometry.centerPos': Position;
        /** (read-only) Returns the geometry as a GeoJSON string, in its own coordinates. Here rather than only on Feature because serialising a shape otherwise means constructing a GeoJSONGeometryWriter, which no string-based binding can do. */
        readonly 'geometry.geoJSON': string;
        readonly 'geometry.type': 'GEOMETRY_TYPE_POINT' | 'GEOMETRY_TYPE_LINE' | 'GEOMETRY_TYPE_POLYGON' | 'GEOMETRY_TYPE_MULTIPOINT' | 'GEOMETRY_TYPE_MULTILINE' | 'GEOMETRY_TYPE_MULTIPOLYGON' | 'GEOMETRY_TYPE_COLLECTION' | number;
        /** Returns the internal id of this vector element. */
        'id': number;
        /** Returns a copy of the vector element meta data map. The changes you make to this map are NOT reflected in the actual meta data of the element. */
        'metaData': Record<string, Json>;
        /** Returns a copy of the vector element meta data map. The changes you make to this map are NOT reflected in the actual meta data of the element. */
        [key: `metaData.${string}`]: Json;
        /** Returns the state of the visibility flag of this vector element. */
        'visible': boolean;
    };
    'massif::VectorElementClickInfo': {
        /** (read-only) Returns the click info. */
        readonly 'clickInfo': ClickInfo;
        /** (read-only) Returns the click position. */
        readonly 'clickPos': Position;
        /** (read-only) Returns the click type. */
        readonly 'clickType': 'CLICK_TYPE_SINGLE' | 'CLICK_TYPE_LONG' | 'CLICK_TYPE_DOUBLE' | 'CLICK_TYPE_DUAL' | number;
        /** (read-only) Returns the position on the clicked element that is closest to the click position: the center for points, the closest point for lines, the anchor point for billboards, getClickPos() for polygons. */
        readonly 'elementClickPos': Position;
        /** (read-only) Returns the layer of the clicked vector element. */
        readonly 'layer': Handle;
        /** Returns the culling delay of the layer in milliseconds. */
        'layer.cullDelay': number;
        /** Returns a copy of the layer meta data map. The changes you make to this map are NOT reflected in the actual meta data of the layer. */
        'layer.metaData': Record<string, Json>;
        /** Returns a copy of the layer meta data map. The changes you make to this map are NOT reflected in the actual meta data of the layer. */
        [key: `layer.metaData.${string}`]: Json;
        /** Returns the opacity of this layer. */
        'layer.opacity': number;
        /** Returns whether this layer goes through the post-process effect. */
        'layer.postProcessed': boolean;
        /** Returns the layer task priority of this layer. */
        'layer.updatePriority': number;
        /** Returns the visibility of this layer. */
        'layer.visible': boolean;
        /** Returns the visible zoom range of this layer. */
        'layer.visibleZoomRange': [number, number];
        /** (read-only) Returns the clicked vector element. */
        readonly 'vectorElement': Handle;
        /** (read-only) Returns the bounds of this vector element. */
        readonly 'vectorElement.bounds': Bounds;
        /** (read-only) Returns the geometry object that defines the location of this vector element. */
        readonly 'vectorElement.geometry': Handle;
        /** (read-only) Returns the minimal bounds for the geometry. */
        readonly 'vectorElement.geometry.bounds': Bounds;
        readonly 'vectorElement.geometry.centerPos': Position;
        /** (read-only) Returns the geometry as a GeoJSON string, in its own coordinates. Here rather than only on Feature because serialising a shape otherwise means constructing a GeoJSONGeometryWriter, which no string-based binding can do. */
        readonly 'vectorElement.geometry.geoJSON': string;
        readonly 'vectorElement.geometry.type': 'GEOMETRY_TYPE_POINT' | 'GEOMETRY_TYPE_LINE' | 'GEOMETRY_TYPE_POLYGON' | 'GEOMETRY_TYPE_MULTIPOINT' | 'GEOMETRY_TYPE_MULTILINE' | 'GEOMETRY_TYPE_MULTIPOLYGON' | 'GEOMETRY_TYPE_COLLECTION' | number;
        /** Returns the internal id of this vector element. */
        'vectorElement.id': number;
        /** Returns a copy of the vector element meta data map. The changes you make to this map are NOT reflected in the actual meta data of the element. */
        'vectorElement.metaData': Record<string, Json>;
        /** Returns a copy of the vector element meta data map. The changes you make to this map are NOT reflected in the actual meta data of the element. */
        [key: `vectorElement.metaData.${string}`]: Json;
        /** Returns the state of the visibility flag of this vector element. */
        'vectorElement.visible': boolean;
    };
    'massif::VectorElementDragInfo': {
        /** (read-only) Returns the drag position in projection coordinate system of the layer. */
        readonly 'mapPos': Position;
        /** (read-only) Returns the drag position in screen coordinates. */
        readonly 'screenPos': [number, number];
        /** (read-only) Returns the vector element being dragged. */
        readonly 'vectorElement': Handle;
        /** (read-only) Returns the bounds of this vector element. */
        readonly 'vectorElement.bounds': Bounds;
        /** (read-only) Returns the geometry object that defines the location of this vector element. */
        readonly 'vectorElement.geometry': Handle;
        /** (read-only) Returns the minimal bounds for the geometry. */
        readonly 'vectorElement.geometry.bounds': Bounds;
        readonly 'vectorElement.geometry.centerPos': Position;
        /** (read-only) Returns the geometry as a GeoJSON string, in its own coordinates. Here rather than only on Feature because serialising a shape otherwise means constructing a GeoJSONGeometryWriter, which no string-based binding can do. */
        readonly 'vectorElement.geometry.geoJSON': string;
        readonly 'vectorElement.geometry.type': 'GEOMETRY_TYPE_POINT' | 'GEOMETRY_TYPE_LINE' | 'GEOMETRY_TYPE_POLYGON' | 'GEOMETRY_TYPE_MULTIPOINT' | 'GEOMETRY_TYPE_MULTILINE' | 'GEOMETRY_TYPE_MULTIPOLYGON' | 'GEOMETRY_TYPE_COLLECTION' | number;
        /** Returns the internal id of this vector element. */
        'vectorElement.id': number;
        /** Returns a copy of the vector element meta data map. The changes you make to this map are NOT reflected in the actual meta data of the element. */
        'vectorElement.metaData': Record<string, Json>;
        /** Returns a copy of the vector element meta data map. The changes you make to this map are NOT reflected in the actual meta data of the element. */
        [key: `vectorElement.metaData.${string}`]: Json;
        /** Returns the state of the visibility flag of this vector element. */
        'vectorElement.visible': boolean;
        /** (read-only) Returns the drag mode. */
        readonly 'vectorElementDragMode': 'VECTOR_ELEMENT_DRAG_MODE_VERTEX' | 'VECTOR_ELEMENT_DRAG_MODE_ELEMENT' | number;
    };
    'massif::VectorElementEventListener': {
    };
    'massif::VectorElementSearchService': {
        /** (read-only) Returns the vector data source of the search service. */
        readonly 'dataSource': Handle;
        /** (read-only) Returns the extent of the data of this data source. The bounds are in coordinate system of the projection of the data source. */
        readonly 'dataSource.dataExtent': Bounds;
        /** (read-only) Returns the projection used by this data source. */
        readonly 'dataSource.projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'dataSource.projection.bounds': Bounds;
        readonly 'dataSource.projection.name': string;
        /** Returns the maximum number of results the search service returns. */
        'maxResults': number;
    };
    'massif::VectorLayer': {
        /** Returns true if Z-buffering is enabled for 2D geometry. By default it is disabled and used only for billboards. */
        'ZBuffering': boolean;
        /** Returns the culling delay of the layer in milliseconds. */
        'cullDelay': number;
        /** (read-only) Returns the vector data source of this vector layer. */
        readonly 'dataSource': Handle;
        /** (read-only) Returns the extent of the data of this data source. The bounds are in coordinate system of the projection of the data source. */
        readonly 'dataSource.dataExtent': Bounds;
        /** (read-only) Returns the projection used by this data source. */
        readonly 'dataSource.projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'dataSource.projection.bounds': Bounds;
        readonly 'dataSource.projection.name': string;
        /** Returns a copy of the layer meta data map. The changes you make to this map are NOT reflected in the actual meta data of the layer. */
        'metaData': Record<string, Json>;
        /** Returns a copy of the layer meta data map. The changes you make to this map are NOT reflected in the actual meta data of the layer. */
        [key: `metaData.${string}`]: Json;
        /** Returns the opacity of this layer. */
        'opacity': number;
        /** Returns whether this layer goes through the post-process effect. */
        'postProcessed': boolean;
        /** Returns the layer task priority of this layer. */
        'updatePriority': number;
        /** Returns the vector element event listener. */
        'vectorElementEventListener': Handle;
        /** Returns the visibility of this layer. */
        'visible': boolean;
        /** Returns the visible zoom range of this layer. */
        'visibleZoomRange': [number, number];
    };
    'massif::VectorTileClickInfo': {
        /** (read-only) Returns the click info. */
        readonly 'clickInfo': ClickInfo;
        /** (read-only) Returns the click position. */
        readonly 'clickPos': Position;
        /** (read-only) Returns the click type. */
        readonly 'clickType': 'CLICK_TYPE_SINGLE' | 'CLICK_TYPE_LONG' | 'CLICK_TYPE_DOUBLE' | 'CLICK_TYPE_DUAL' | number;
        /** (read-only) Returns the clicked feature. */
        readonly 'feature': Handle;
        readonly 'feature.distance': number;
        /** (read-only) Returns the geometry of the feature. */
        readonly 'feature.geometry': Handle;
        /** (read-only) Returns the minimal bounds for the geometry. */
        readonly 'feature.geometry.bounds': Bounds;
        readonly 'feature.geometry.centerPos': Position;
        /** (read-only) Returns the geometry as a GeoJSON string, in its own coordinates. Here rather than only on Feature because serialising a shape otherwise means constructing a GeoJSONGeometryWriter, which no string-based binding can do. */
        readonly 'feature.geometry.geoJSON': string;
        readonly 'feature.geometry.type': 'GEOMETRY_TYPE_POINT' | 'GEOMETRY_TYPE_LINE' | 'GEOMETRY_TYPE_POLYGON' | 'GEOMETRY_TYPE_MULTIPOINT' | 'GEOMETRY_TYPE_MULTILINE' | 'GEOMETRY_TYPE_MULTIPOLYGON' | 'GEOMETRY_TYPE_COLLECTION' | number;
        /** (read-only) Returns the feature's geometry as a GeoJSON string, in its own coordinates. Serialising a geometry otherwise means constructing a GeoJSONGeometryWriter in the binding, which every binding then does differently and, in a scripting one, slowly. */
        readonly 'feature.geometryGeoJSON': string;
        /** (read-only) Returns the id of the feature. */
        readonly 'feature.id': number;
        /** (read-only) Returns the layer name of the feature. */
        readonly 'feature.layerName': string;
        /** (read-only) Returns the map tile of the feature. */
        readonly 'feature.mapTile': Tile;
        /** (read-only) Returns the properties of the feature. */
        readonly 'feature.properties': Json;
        /** (read-only) Returns the position on the clicked feature that is closest to the click position: the center for points, the closest point for lines, the anchor point for billboards, getClickPos() for polygons. */
        readonly 'featureClickPos': Position;
        /** (read-only) Returns the id of the clicked feature. */
        readonly 'featureId': number;
        /** (read-only) Returns the name of the layer of the clicked feature. Note that this is the layer name in the tile, not the name of style layer. */
        readonly 'featureLayerName': string;
        /** (read-only) Returns the position of the clicked feature. For a MultiPoint this is the clicked point (see getFeaturePosIndex), not the centre of the set. */
        readonly 'featurePos': Position;
        /** (read-only) In case of MultiPoint PointGeometry this will return the index of the clicked position */
        readonly 'featurePosIndex': number;
        /** (read-only) Returns the layer of the vector tile. */
        readonly 'layer': Handle;
        /** Returns the culling delay of the layer in milliseconds. */
        'layer.cullDelay': number;
        /** Returns a copy of the layer meta data map. The changes you make to this map are NOT reflected in the actual meta data of the layer. */
        'layer.metaData': Record<string, Json>;
        /** Returns a copy of the layer meta data map. The changes you make to this map are NOT reflected in the actual meta data of the layer. */
        [key: `layer.metaData.${string}`]: Json;
        /** Returns the opacity of this layer. */
        'layer.opacity': number;
        /** Returns whether this layer goes through the post-process effect. */
        'layer.postProcessed': boolean;
        /** Returns the layer task priority of this layer. */
        'layer.updatePriority': number;
        /** Returns the visibility of this layer. */
        'layer.visible': boolean;
        /** Returns the visible zoom range of this layer. */
        'layer.visibleZoomRange': [number, number];
        /** (read-only) Returns the tile id of the clicked feature. */
        readonly 'mapTile': Tile;
    };
    'massif::VectorTileDecoder': {
        readonly 'maxZoom': number;
        readonly 'minZoom': number;
    };
    'massif::VectorTileEventListener': {
    };
    'massif::VectorTileFeature': {
        readonly 'distance': number;
        /** (read-only) Returns the geometry of the feature. */
        readonly 'geometry': Handle;
        /** (read-only) Returns the minimal bounds for the geometry. */
        readonly 'geometry.bounds': Bounds;
        readonly 'geometry.centerPos': Position;
        /** (read-only) Returns the geometry as a GeoJSON string, in its own coordinates. Here rather than only on Feature because serialising a shape otherwise means constructing a GeoJSONGeometryWriter, which no string-based binding can do. */
        readonly 'geometry.geoJSON': string;
        readonly 'geometry.type': 'GEOMETRY_TYPE_POINT' | 'GEOMETRY_TYPE_LINE' | 'GEOMETRY_TYPE_POLYGON' | 'GEOMETRY_TYPE_MULTIPOINT' | 'GEOMETRY_TYPE_MULTILINE' | 'GEOMETRY_TYPE_MULTIPOLYGON' | 'GEOMETRY_TYPE_COLLECTION' | number;
        /** (read-only) Returns the feature's geometry as a GeoJSON string, in its own coordinates. Serialising a geometry otherwise means constructing a GeoJSONGeometryWriter in the binding, which every binding then does differently and, in a scripting one, slowly. */
        readonly 'geometryGeoJSON': string;
        /** (read-only) Returns the id of the feature. */
        readonly 'id': number;
        /** (read-only) Returns the layer name of the feature. */
        readonly 'layerName': string;
        /** (read-only) Returns the map tile of the feature. */
        readonly 'mapTile': Tile;
        /** (read-only) Returns the properties of the feature. */
        readonly 'properties': Json;
    };
    'massif::VectorTileFeatureBuilder': {
        /** Returns the geometry of the builder. */
        'geometry': Handle;
        /** (read-only) Returns the minimal bounds for the geometry. */
        readonly 'geometry.bounds': Bounds;
        readonly 'geometry.centerPos': Position;
        /** (read-only) Returns the geometry as a GeoJSON string, in its own coordinates. Here rather than only on Feature because serialising a shape otherwise means constructing a GeoJSONGeometryWriter, which no string-based binding can do. */
        readonly 'geometry.geoJSON': string;
        readonly 'geometry.type': 'GEOMETRY_TYPE_POINT' | 'GEOMETRY_TYPE_LINE' | 'GEOMETRY_TYPE_POLYGON' | 'GEOMETRY_TYPE_MULTIPOINT' | 'GEOMETRY_TYPE_MULTILINE' | 'GEOMETRY_TYPE_MULTIPOLYGON' | 'GEOMETRY_TYPE_COLLECTION' | number;
        /** Returns the id of the builder. */
        'id': number;
        /** Returns the layer name of the builder. */
        'layerName': string;
        /** Returns the map tile of the builder. */
        'mapTile': Tile;
    };
    'massif::VectorTileFeatureCollection': {
        /** (read-only) Returns the number of features in this container. */
        readonly 'featureCount': number;
    };
    'massif::VectorTileLayer': {
        /** Returns the tile data source of the associated UTF grid. By default this is null. */
        'UTFGridDataSource': Handle;
        /** (read-only) Returns the extent of the tiles in this data source. The bounds are in coordinate system of the projection of the data source. */
        readonly 'UTFGridDataSource.dataExtent': Bounds;
        /** Gets the current maximum overzoom level for this datasource. Over it the datasource will not be "drawn" */
        'UTFGridDataSource.maxOverzoomLevel': number;
        /** (read-only) Returns the maximum zoom level supported by this data source. */
        readonly 'UTFGridDataSource.maxZoom': number;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        'UTFGridDataSource.metaData': Record<string, Json>;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        [key: `UTFGridDataSource.metaData.${string}`]: Json;
        /** (read-only) Returns the minimum zoom level supported by this data source. */
        readonly 'UTFGridDataSource.minZoom': number;
        /** (read-only) Returns the projection of this tile source. */
        readonly 'UTFGridDataSource.projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'UTFGridDataSource.projection.bounds': Bounds;
        readonly 'UTFGridDataSource.projection.name': string;
        /** Returns the UTF grid event listener. */
        'UTFGridEventListener': Handle;
        /** Returns the current display order of the buildings. LAST draws over flat labels too: a label that must clear a building is a billboard one, whose pass runs after the buildings. */
        'buildingRenderOrder': 'VECTOR_TILE_RENDER_ORDER_HIDDEN' | 'VECTOR_TILE_RENDER_ORDER_LAYER' | 'VECTOR_TILE_RENDER_ORDER_LAST' | number;
        /** Returns the click handler layer filter. The filter is given as ECMA regular expression that is applied to qualified layer names. */
        'clickHandlerLayerFilter': string;
        /** Returns the click radius of vector tile features. Units are screen density independent pixels (DP or DIP). */
        'clickRadius': number;
        /** Returns the culling delay of the layer in milliseconds. */
        'cullDelay': number;
        /** (read-only) Returns the data source assigned to this layer. */
        readonly 'dataSource': Handle;
        /** (read-only) Returns the extent of the tiles in this data source. The bounds are in coordinate system of the projection of the data source. */
        readonly 'dataSource.dataExtent': Bounds;
        /** Gets the current maximum overzoom level for this datasource. Over it the datasource will not be "drawn" */
        'dataSource.maxOverzoomLevel': number;
        /** (read-only) Returns the maximum zoom level supported by this data source. */
        readonly 'dataSource.maxZoom': number;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        'dataSource.metaData': Record<string, Json>;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        [key: `dataSource.metaData.${string}`]: Json;
        /** (read-only) Returns the minimum zoom level supported by this data source. */
        readonly 'dataSource.minZoom': number;
        /** (read-only) Returns the projection of this tile source. */
        readonly 'dataSource.projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'dataSource.projection.bounds': Bounds;
        readonly 'dataSource.projection.name': string;
        /** Returns the current frame number. */
        'frameNr': number;
        /** Returns the label blending speed, in full fades per second. */
        'labelBlendingSpeed': number;
        /** Returns how much of the perspective divide a label keeps as it recedes from the camera. */
        'labelPerspectiveScaling': number;
        /** Returns the current display order of the labels. */
        'labelRenderOrder': 'VECTOR_TILE_RENDER_ORDER_HIDDEN' | 'VECTOR_TILE_RENDER_ORDER_LAYER' | 'VECTOR_TILE_RENDER_ORDER_LAST' | number;
        /** Returns the current relative layer blending speed. */
        'layerBlendingSpeed': number;
        /** Gets the current maximum overzoom level for this layer. */
        'maxOverzoomLevel': number;
        /** Gets the current maximum underzoom level for this layer. */
        'maxUnderzoomLevel': number;
        /** Returns a copy of the layer meta data map. The changes you make to this map are NOT reflected in the actual meta data of the layer. */
        'metaData': Record<string, Json>;
        /** Returns a copy of the layer meta data map. The changes you make to this map are NOT reflected in the actual meta data of the layer. */
        [key: `metaData.${string}`]: Json;
        /** Returns the opacity of this layer. */
        'opacity': number;
        /** Returns whether this layer goes through the post-process effect. */
        'postProcessed': boolean;
        /** Returns the state of the preloading flag of this layer. */
        'preloading': boolean;
        readonly 'preloadingTileCount': number;
        /** (read-only) Returns the projection this layer's data is in, which is its data source's. */
        readonly 'projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'projection.bounds': Bounds;
        readonly 'projection.name': string;
        /** Returns the renderer layer filter. The filter is given as ECMA regular expression that is applied to qualified layer names. */
        'rendererLayerFilter': string;
        /** (read-only) Returns the data source assigned to this layer. */
        readonly 'source': Handle;
        /** (read-only) Returns the extent of the tiles in this data source. The bounds are in coordinate system of the projection of the data source. */
        readonly 'source.dataExtent': Bounds;
        /** Gets the current maximum overzoom level for this datasource. Over it the datasource will not be "drawn" */
        'source.maxOverzoomLevel': number;
        /** (read-only) Returns the maximum zoom level supported by this data source. */
        readonly 'source.maxZoom': number;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        'source.metaData': Record<string, Json>;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        [key: `source.metaData.${string}`]: Json;
        /** (read-only) Returns the minimum zoom level supported by this data source. */
        readonly 'source.minZoom': number;
        /** (read-only) Returns the projection of this tile source. */
        readonly 'source.projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'source.projection.bounds': Bounds;
        readonly 'source.projection.name': string;
        /** (read-only) Returns the tile decoder assigned to this layer. */
        readonly 'style': Handle;
        readonly 'style.maxZoom': number;
        readonly 'style.minZoom': number;
        /** Returns the state of the synchronized refresh flag. */
        'synchronizedRefresh': boolean;
        /** Returns the tile cache capacity. */
        'tileCacheCapacity': number;
        /** (read-only) Returns the tile decoder assigned to this layer. */
        readonly 'tileDecoder': Handle;
        readonly 'tileDecoder.maxZoom': number;
        readonly 'tileDecoder.minZoom': number;
        /** Returns the tile load listener. */
        'tileLoadListener': Handle;
        /** Returns the current tile substitution policy. */
        'tileSubstitutionPolicy': 'TILE_SUBSTITUTION_POLICY_ALL' | 'TILE_SUBSTITUTION_POLICY_VISIBLE' | 'TILE_SUBSTITUTION_POLICY_NONE' | number;
        /** Returns the layer task priority of this layer. */
        'updatePriority': number;
        /** Returns the vector tile event listener. */
        'vectorTileEventListener': Handle;
        /** Returns the visibility of this layer. */
        'visible': boolean;
        /** (read-only) How many tiles the last cull put on screen. A diagnostic: it is what the tile LOD numbers actually cost. */
        readonly 'visibleTileCount': number;
        /** Returns the visible zoom range of this layer. */
        'visibleZoomRange': [number, number];
        /** Gets the current zoom level bias for this layer. */
        'zoomLevelBias': number;
    };
    'massif::VectorTileSearchService': {
        /** (read-only) Returns the tile data source of the search service. */
        readonly 'dataSource': Handle;
        /** (read-only) Returns the extent of the tiles in this data source. The bounds are in coordinate system of the projection of the data source. */
        readonly 'dataSource.dataExtent': Bounds;
        /** Gets the current maximum overzoom level for this datasource. Over it the datasource will not be "drawn" */
        'dataSource.maxOverzoomLevel': number;
        /** (read-only) Returns the maximum zoom level supported by this data source. */
        readonly 'dataSource.maxZoom': number;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        'dataSource.metaData': Record<string, Json>;
        /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
        [key: `dataSource.metaData.${string}`]: Json;
        /** (read-only) Returns the minimum zoom level supported by this data source. */
        readonly 'dataSource.minZoom': number;
        /** (read-only) Returns the projection of this tile source. */
        readonly 'dataSource.projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'dataSource.projection.bounds': Bounds;
        readonly 'dataSource.projection.name': string;
        /** Returns the layers to filter while decoding tiles. */
        'layers': string[];
        /** Returns the maximum number of results the search service returns. */
        'maxResults': number;
        /** Returns the maximum zoom level of vector tiles used. By default the maximum zoom level is specified by data source. */
        'maxZoom': number;
        /** Returns the minimum zoom level of vector tiles used. By default the minimum zoom level is specified by data source and is usually 0. */
        'minZoom': number;
        /** Returns wether to prevent duplicate elements */
        'preventDuplicates': boolean;
        /** (read-only) Returns the projection the results are expressed in, which is the data source's. */
        readonly 'projection': Handle;
        /** (read-only) Returns the bounds of this projection. */
        readonly 'projection.bounds': Bounds;
        readonly 'projection.name': string;
        /** Returns wether result features are sorted by distance */
        'sortByDistance': boolean;
        /** (read-only) Returns the tile decoder used by the search service. */
        readonly 'tileDecoder': Handle;
        readonly 'tileDecoder.maxZoom': number;
        readonly 'tileDecoder.minZoom': number;
    };
    'massif::ViewState': {
        /** (read-only) Returns the dots per inch parameter of the screen. */
        readonly 'DPI': number;
        /** (read-only) Returns a value that is used for converting display independent pixels (dp) to pixels (px). This values depends on the screen density. */
        readonly 'DPToPX': number;
        /** (read-only) Returns the vertical field of view angle. */
        readonly 'FOVY': number;
        /** (read-only) Returns the aspect ratio of the map screen. Equal to width / height. */
        readonly 'aspectRatio': number;
        /** (read-only) Returns the state of the camera changed flag. */
        readonly 'cameraChanged': boolean;
        /** (read-only) Returns the far plane distance. */
        readonly 'far': number;
        /** (read-only) Returns the height of the map screen. */
        readonly 'height': number;
        /** (read-only) Returns the near plane distance. */
        readonly 'near': number;
        /** (read-only) Returns the camera rotation angle. */
        readonly 'rotation': number;
        /** (read-only) Returns the screen height. */
        readonly 'screenHeight': number;
        /** (read-only) Returns the screen width. */
        readonly 'screenWidth': number;
        /** (read-only) Returns the camera tilt angle. A negative tilt means the view looks above the horizon. */
        readonly 'tilt': number;
        /** (read-only) Returns the conversion ratio between internal map units and screen density independent pixels (DP or DIP). This parameter is dependent on the zoom level, DPI and other screen parameters. */
        readonly 'unitToDPCoef': number;
        /** (read-only) Returns the conversion ratio between internal map units and screen pixels. This parameter is dependent on the zoom level and other screen parameters. */
        readonly 'unitToPXCoef': number;
        /** (read-only) Returns the width of the map screen. */
        readonly 'width': number;
        /** (read-only) Returns the camera zoom level. */
        readonly 'zoom': number;
        /** (read-only) Returns the distance between the focus and the camera position, when the zoom level is set to 0. This parameter depends on the screen size, DPI, tile draw size and field of view settings. */
        readonly 'zoom0Distance': number;
    };
    'massif::WKBGeometryReader': {
    };
    'massif::WKBGeometryWriter': {
        /** Returns the endianness of output format. */
        'bigEndian': boolean;
        /** Returns the state of Z coordinate serialization. */
        'z': boolean;
    };
    'massif::WKTGeometryReader': {
    };
    'massif::WKTGeometryWriter': {
        /** Returns the state of Z coordinate serialization. */
        'z': boolean;
    };
    'massif::ZippedAssetPackage': {
        readonly 'assetNames': string[];
        readonly 'localAssetNames': string[];
    };
}

/** @internal Per class, the paths flagged as coordinates - see PositionPath. */
export interface PositionPaths {
    'massif::Address': {
    };
    'massif::AnimationStyle': {
    };
    'massif::AnimationStyleBuilder': {
    };
    'massif::AssetPackage': {
    };
    'massif::AssetTileDataSource': {
        'dataExtent': true;
        'projection.bounds': true;
    };
    'massif::BalloonPopup': {
        'baseBillboard.bounds': true;
        'baseBillboard.geometry.bounds': true;
        'baseBillboard.geometry.centerPos': true;
        'baseBillboard.rootGeometry.bounds': true;
        'baseBillboard.rootGeometry.centerPos': true;
        'bounds': true;
        'geometry.bounds': true;
        'geometry.centerPos': true;
        'rootGeometry.bounds': true;
        'rootGeometry.centerPos': true;
    };
    'massif::BalloonPopupButton': {
    };
    'massif::BalloonPopupButtonClickInfo': {
        'vectorElement.bounds': true;
        'vectorElement.geometry.bounds': true;
        'vectorElement.geometry.centerPos': true;
    };
    'massif::BalloonPopupButtonStyle': {
    };
    'massif::BalloonPopupButtonStyleBuilder': {
    };
    'massif::BalloonPopupEventListener': {
    };
    'massif::BalloonPopupMargins': {
    };
    'massif::BalloonPopupStyle': {
    };
    'massif::BalloonPopupStyleBuilder': {
    };
    'massif::BaseMapView': {
        'cameraPos': true;
        'focusPos': true;
    };
    'massif::Billboard': {
        'bounds': true;
        'geometry.bounds': true;
        'geometry.centerPos': true;
        'rootGeometry.bounds': true;
        'rootGeometry.centerPos': true;
    };
    'massif::BillboardStyle': {
    };
    'massif::BillboardStyleBuilder': {
    };
    'massif::BinaryData': {
    };
    'massif::Bitmap': {
    };
    'massif::BitmapOverlayRasterTileDataSource': {
        'dataExtent': true;
        'projection.bounds': true;
    };
    'massif::BundleAssetPackage': {
    };
    'massif::CacheTileDataSource': {
        'dataExtent': true;
        'dataSource.dataExtent': true;
        'dataSource.projection.bounds': true;
        'projection.bounds': true;
    };
    'massif::CartoCSSStyleSet': {
    };
    'massif::CelestialArc': {
        'position': true;
    };
    'massif::CelestialClickInfo': {
        'celestialObject.position': true;
    };
    'massif::CelestialEventListener': {
    };
    'massif::CelestialLabel': {
        'position': true;
    };
    'massif::CelestialLayer': {
    };
    'massif::CelestialObject': {
        'position': true;
    };
    'massif::CelestialSprite': {
        'position': true;
    };
    'massif::ClickInfo': {
    };
    'massif::ClusterElementBuilder': {
    };
    'massif::ClusterFetchTask': {
    };
    'massif::ClusteredVectorLayer': {
        'dataSource.dataExtent': true;
        'dataSource.projection.bounds': true;
    };
    'massif::Color': {
    };
    'massif::CombinedTileDataSource': {
        'dataExtent': true;
        'projection.bounds': true;
    };
    'massif::CompiledStyleSet': {
    };
    'massif::CompositeVectorTileLayer': {
        'UTFGridDataSource.dataExtent': true;
        'UTFGridDataSource.projection.bounds': true;
        'dataSource.dataExtent': true;
        'dataSource.projection.bounds': true;
        'projection.bounds': true;
        'source.dataExtent': true;
        'source.projection.bounds': true;
    };
    'massif::ContourTileDataSource': {
        'dataExtent': true;
        'projection.bounds': true;
    };
    'massif::CullState': {
    };
    'massif::CustomPopup': {
        'baseBillboard.bounds': true;
        'baseBillboard.geometry.bounds': true;
        'baseBillboard.geometry.centerPos': true;
        'baseBillboard.rootGeometry.bounds': true;
        'baseBillboard.rootGeometry.centerPos': true;
        'bounds': true;
        'geometry.bounds': true;
        'geometry.centerPos': true;
        'rootGeometry.bounds': true;
        'rootGeometry.centerPos': true;
    };
    'massif::CustomPopupHandler': {
    };
    'massif::CustomRasterTileLayer': {
        'UTFGridDataSource.dataExtent': true;
        'UTFGridDataSource.projection.bounds': true;
        'dataSource.dataExtent': true;
        'dataSource.projection.bounds': true;
        'projection.bounds': true;
        'source.dataExtent': true;
        'source.projection.bounds': true;
    };
    'massif::DataSourceListener': {
    };
    'massif::DirAssetPackage': {
    };
    'massif::DouglasPeuckerGeometrySimplifier': {
    };
    'massif::DownloadTask': {
    };
    'massif::EPSG3857': {
        'bounds': true;
    };
    'massif::EPSG4326': {
        'bounds': true;
    };
    'massif::EditableVectorLayer': {
        'dataSource.dataExtent': true;
        'dataSource.projection.bounds': true;
        'selectedVectorElement.bounds': true;
        'selectedVectorElement.geometry.bounds': true;
        'selectedVectorElement.geometry.centerPos': true;
    };
    'massif::ElevationDecoder': {
    };
    'massif::EventListener': {
    };
    'massif::Feature': {
        'geometry.bounds': true;
        'geometry.centerPos': true;
    };
    'massif::FeatureBuilder': {
        'geometry.bounds': true;
        'geometry.centerPos': true;
    };
    'massif::FeatureCollection': {
    };
    'massif::FeatureCollectionSearchService': {
        'projection.bounds': true;
    };
    'massif::FetchTask': {
    };
    'massif::FetchTaskBase': {
    };
    'massif::FetchingTasks': {
    };
    'massif::FetchingTileTasks': {
    };
    'massif::FogOptions': {
    };
    'massif::GeoJSONGeometryReader': {
        'targetProjection.bounds': true;
    };
    'massif::GeoJSONGeometryWriter': {
        'sourceProjection.bounds': true;
    };
    'massif::GeoJSONVectorTileDataSource': {
        'dataExtent': true;
        'projection.bounds': true;
    };
    'massif::GeocodingAddress': {
    };
    'massif::GeocodingRequest': {
        'location': true;
        'projection.bounds': true;
    };
    'massif::GeocodingResult': {
        'projection.bounds': true;
    };
    'massif::GeocodingService': {
    };
    'massif::Geometry': {
        'bounds': true;
        'centerPos': true;
    };
    'massif::GeometryCollection': {
        'bounds': true;
        'geometry.bounds': true;
        'geometry.centerPos': true;
    };
    'massif::GeometryCollectionStyle': {
    };
    'massif::GeometryCollectionStyleBuilder': {
    };
    'massif::GeometrySimplifier': {
    };
    'massif::HTTPTileDataSource': {
        'dataExtent': true;
        'projection.bounds': true;
    };
    'massif::HillshadeRasterTileLayer': {
        'UTFGridDataSource.dataExtent': true;
        'UTFGridDataSource.projection.bounds': true;
        'dataSource.dataExtent': true;
        'dataSource.projection.bounds': true;
        'projection.bounds': true;
        'source.dataExtent': true;
        'source.projection.bounds': true;
    };
    'massif::Label': {
        'baseBillboard.bounds': true;
        'baseBillboard.geometry.bounds': true;
        'baseBillboard.geometry.centerPos': true;
        'baseBillboard.rootGeometry.bounds': true;
        'baseBillboard.rootGeometry.centerPos': true;
        'bounds': true;
        'geometry.bounds': true;
        'geometry.centerPos': true;
        'rootGeometry.bounds': true;
        'rootGeometry.centerPos': true;
    };
    'massif::LabelStyle': {
    };
    'massif::LabelStyleBuilder': {
    };
    'massif::Layer': {
    };
    'massif::Layers': {
    };
    'massif::LightOptions': {
    };
    'massif::LightStop': {
    };
    'massif::Line': {
        'bounds': true;
        'geometry.bounds': true;
        'geometry.centerPos': true;
    };
    'massif::LineGeometry': {
        'bounds': true;
        'centerPos': true;
    };
    'massif::LineStyle': {
    };
    'massif::LineStyleBuilder': {
    };
    'massif::LocalVectorDataSource': {
        'dataExtent': true;
        'projection.bounds': true;
    };
    'massif::Log': {
    };
    'massif::LogEventListener': {
    };
    'massif::MBTilesTileDataSource': {
        'dataExtent': true;
        'projection.bounds': true;
    };
    'massif::MBVectorTileDecoder': {
    };
    'massif::ManeuverArrowBuilder': {
    };
    'massif::MapBounds': {
        'center': true;
        'max': true;
        'min': true;
    };
    'massif::MapBoxElevationDataDecoder': {
    };
    'massif::MapBoxOnlineGeocodingService': {
    };
    'massif::MapBoxOnlineReverseGeocodingService': {
    };
    'massif::MapClickInfo': {
        'clickPos': true;
    };
    'massif::MapEnvelope': {
        'bounds': true;
    };
    'massif::MapEventListener': {
    };
    'massif::MapInteractionInfo': {
    };
    'massif::MapMoveInfo': {
    };
    'massif::MapPos': {
    };
    'massif::MapRange': {
    };
    'massif::MapRenderer': {
    };
    'massif::MapRendererListener': {
    };
    'massif::MapTile': {
    };
    'massif::MapTilerOnlineTileDataSource': {
        'dataExtent': true;
        'projection.bounds': true;
    };
    'massif::MapVec': {
    };
    'massif::Marker': {
        'baseBillboard.bounds': true;
        'baseBillboard.geometry.bounds': true;
        'baseBillboard.geometry.centerPos': true;
        'baseBillboard.rootGeometry.bounds': true;
        'baseBillboard.rootGeometry.centerPos': true;
        'bounds': true;
        'geometry.bounds': true;
        'geometry.centerPos': true;
        'rootGeometry.bounds': true;
        'rootGeometry.centerPos': true;
    };
    'massif::MarkerStyle': {
    };
    'massif::MarkerStyleBuilder': {
    };
    'massif::MassifApi': {
    };
    'massif::MassifInterop': {
    };
    'massif::MemoryCacheTileDataSource': {
        'dataExtent': true;
        'dataSource.dataExtent': true;
        'dataSource.projection.bounds': true;
        'projection.bounds': true;
    };
    'massif::MergedMBVTTileDataSource': {
        'dataExtent': true;
        'projection.bounds': true;
    };
    'massif::MultiGeometry': {
        'bounds': true;
        'centerPos': true;
    };
    'massif::MultiLineGeometry': {
        'bounds': true;
        'centerPos': true;
    };
    'massif::MultiOSMOfflineGeocodingService': {
    };
    'massif::MultiOSMOfflineReverseGeocodingService': {
    };
    'massif::MultiPointGeometry': {
        'bounds': true;
        'centerPos': true;
    };
    'massif::MultiPolygonGeometry': {
        'bounds': true;
        'centerPos': true;
    };
    'massif::MultiTileDataSource': {
        'dataExtent': true;
        'projection.bounds': true;
    };
    'massif::MultiValhallaOfflineRoutingService': {
    };
    'massif::NMLModel': {
        'baseBillboard.bounds': true;
        'baseBillboard.geometry.bounds': true;
        'baseBillboard.geometry.centerPos': true;
        'baseBillboard.rootGeometry.bounds': true;
        'baseBillboard.rootGeometry.centerPos': true;
        'bounds': true;
        'geometry.bounds': true;
        'geometry.centerPos': true;
        'rootGeometry.bounds': true;
        'rootGeometry.centerPos': true;
    };
    'massif::NMLModelStyle': {
    };
    'massif::NMLModelStyleBuilder': {
    };
    'massif::OSMOfflineGeocodingService': {
    };
    'massif::OSMOfflineReverseGeocodingService': {
    };
    'massif::OSRMOfflineRoutingService': {
    };
    'massif::OnChangeListener': {
    };
    'massif::Options': {
        'baseProjection.bounds': true;
        'panBounds': true;
        'projection.bounds': true;
    };
    'massif::OptionsListener': {
    };
    'massif::OrderedTileDataSource': {
        'dataExtent': true;
        'projection.bounds': true;
    };
    'massif::PMTilesTileDataSource': {
        'dataExtent': true;
        'projection.bounds': true;
    };
    'massif::PackageInfo': {
    };
    'massif::PackageManager': {
    };
    'massif::PackageManagerGeocodingService': {
    };
    'massif::PackageManagerListener': {
    };
    'massif::PackageManagerReverseGeocodingService': {
    };
    'massif::PackageManagerRoutingService': {
    };
    'massif::PackageManagerTileDataSource': {
        'dataExtent': true;
        'projection.bounds': true;
    };
    'massif::PackageManagerValhallaRoutingService': {
    };
    'massif::PackageMetaInfo': {
    };
    'massif::PackageStatus': {
    };
    'massif::PackageTileMask': {
    };
    'massif::PeliasOnlineGeocodingService': {
    };
    'massif::PeliasOnlineReverseGeocodingService': {
    };
    'massif::PersistentCacheTileDataSource': {
        'dataExtent': true;
        'dataSource.dataExtent': true;
        'dataSource.projection.bounds': true;
        'projection.bounds': true;
    };
    'massif::PersistentTaskQueue': {
    };
    'massif::Point': {
        'bounds': true;
        'geometry.bounds': true;
        'geometry.centerPos': true;
        'geometry.pos': true;
    };
    'massif::PointDetailTileDataSource': {
        'dataExtent': true;
        'projection.bounds': true;
    };
    'massif::PointGeometry': {
        'bounds': true;
        'centerPos': true;
        'pos': true;
    };
    'massif::PointStyle': {
    };
    'massif::PointStyleBuilder': {
    };
    'massif::Polygon': {
        'bounds': true;
        'geometry.bounds': true;
        'geometry.centerPos': true;
    };
    'massif::Polygon3D': {
        'bounds': true;
        'geometry.bounds': true;
        'geometry.centerPos': true;
    };
    'massif::Polygon3DStyle': {
    };
    'massif::Polygon3DStyleBuilder': {
    };
    'massif::PolygonGeometry': {
        'bounds': true;
        'centerPos': true;
    };
    'massif::PolygonStyle': {
    };
    'massif::PolygonStyleBuilder': {
    };
    'massif::Popup': {
        'baseBillboard.bounds': true;
        'baseBillboard.geometry.bounds': true;
        'baseBillboard.geometry.centerPos': true;
        'baseBillboard.rootGeometry.bounds': true;
        'baseBillboard.rootGeometry.centerPos': true;
        'bounds': true;
        'geometry.bounds': true;
        'geometry.centerPos': true;
        'rootGeometry.bounds': true;
        'rootGeometry.centerPos': true;
    };
    'massif::PopupClickInfo': {
        'clickPos': true;
        'popup.baseBillboard.bounds': true;
        'popup.bounds': true;
        'popup.geometry.bounds': true;
        'popup.geometry.centerPos': true;
        'popup.rootGeometry.bounds': true;
        'popup.rootGeometry.centerPos': true;
    };
    'massif::PopupDrawInfo': {
        'popup.baseBillboard.bounds': true;
        'popup.bounds': true;
        'popup.geometry.bounds': true;
        'popup.geometry.centerPos': true;
        'popup.rootGeometry.bounds': true;
        'popup.rootGeometry.centerPos': true;
    };
    'massif::PopupStyle': {
    };
    'massif::PopupStyleBuilder': {
    };
    'massif::PostProcessEffect': {
    };
    'massif::Projection': {
        'bounds': true;
    };
    'massif::RasterTileClickInfo': {
        'clickPos': true;
    };
    'massif::RasterTileEventListener': {
    };
    'massif::RasterTileLayer': {
        'UTFGridDataSource.dataExtent': true;
        'UTFGridDataSource.projection.bounds': true;
        'dataSource.dataExtent': true;
        'dataSource.projection.bounds': true;
        'projection.bounds': true;
        'source.dataExtent': true;
        'source.projection.bounds': true;
    };
    'massif::RedrawRequestListener': {
    };
    'massif::RendererCaptureListener': {
    };
    'massif::ReverseGeocodingRequest': {
        'location': true;
        'projection.bounds': true;
    };
    'massif::ReverseGeocodingService': {
    };
    'massif::RouteMatchingEdge': {
    };
    'massif::RouteMatchingPoint': {
        'pos': true;
    };
    'massif::RouteMatchingRequest': {
        'projection.bounds': true;
    };
    'massif::RouteMatchingResult': {
        'projection.bounds': true;
    };
    'massif::RoutingInstruction': {
    };
    'massif::RoutingRequest': {
        'projection.bounds': true;
    };
    'massif::RoutingResult': {
        'projection.bounds': true;
    };
    'massif::RoutingService': {
    };
    'massif::SGREOfflineRoutingService': {
    };
    'massif::ScreenBounds': {
    };
    'massif::ScreenPos': {
    };
    'massif::SearchRequest': {
        'geometry.bounds': true;
        'geometry.centerPos': true;
        'projection.bounds': true;
    };
    'massif::SkyOptions': {
    };
    'massif::SolidLayer': {
    };
    'massif::Style': {
    };
    'massif::StyleBuilder': {
    };
    'massif::TerrainOptions': {
    };
    'massif::TerrariumElevationDataDecoder': {
    };
    'massif::Text': {
        'baseBillboard.bounds': true;
        'baseBillboard.geometry.bounds': true;
        'baseBillboard.geometry.centerPos': true;
        'baseBillboard.rootGeometry.bounds': true;
        'baseBillboard.rootGeometry.centerPos': true;
        'bounds': true;
        'geometry.bounds': true;
        'geometry.centerPos': true;
        'rootGeometry.bounds': true;
        'rootGeometry.centerPos': true;
    };
    'massif::TextMargins': {
    };
    'massif::TextStyle': {
    };
    'massif::TextStyleBuilder': {
    };
    'massif::TileData': {
    };
    'massif::TileDataSource': {
        'dataExtent': true;
        'projection.bounds': true;
    };
    'massif::TileDecoderListener': {
    };
    'massif::TileDownloadInfo': {
    };
    'massif::TileDownloadListener': {
    };
    'massif::TileInfo': {
    };
    'massif::TileLayer': {
        'UTFGridDataSource.dataExtent': true;
        'UTFGridDataSource.projection.bounds': true;
        'dataSource.dataExtent': true;
        'dataSource.projection.bounds': true;
        'projection.bounds': true;
        'source.dataExtent': true;
        'source.projection.bounds': true;
    };
    'massif::TileLoadListener': {
    };
    'massif::TileUtils': {
    };
    'massif::TomTomOnlineGeocodingService': {
    };
    'massif::TomTomOnlineReverseGeocodingService': {
    };
    'massif::TorqueTileDecoder': {
    };
    'massif::TorqueTileLayer': {
        'UTFGridDataSource.dataExtent': true;
        'UTFGridDataSource.projection.bounds': true;
        'dataSource.dataExtent': true;
        'dataSource.projection.bounds': true;
        'projection.bounds': true;
        'source.dataExtent': true;
        'source.projection.bounds': true;
    };
    'massif::TouchHandlerListener': {
    };
    'massif::UTFGridClickInfo': {
        'clickPos': true;
    };
    'massif::UTFGridEventListener': {
    };
    'massif::UiDispatcher': {
    };
    'massif::ValhallaOfflineRoutingService': {
    };
    'massif::ValhallaOnlineRoutingService': {
    };
    'massif::Variant': {
    };
    'massif::VariantArrayBuilder': {
    };
    'massif::VariantObjectBuilder': {
    };
    'massif::VectorData': {
    };
    'massif::VectorDataSource': {
        'dataExtent': true;
        'projection.bounds': true;
    };
    'massif::VectorEditEventListener': {
    };
    'massif::VectorElement': {
        'bounds': true;
        'geometry.bounds': true;
        'geometry.centerPos': true;
    };
    'massif::VectorElementClickInfo': {
        'clickPos': true;
        'elementClickPos': true;
        'vectorElement.bounds': true;
        'vectorElement.geometry.bounds': true;
        'vectorElement.geometry.centerPos': true;
    };
    'massif::VectorElementDragInfo': {
        'mapPos': true;
        'vectorElement.bounds': true;
        'vectorElement.geometry.bounds': true;
        'vectorElement.geometry.centerPos': true;
    };
    'massif::VectorElementEventListener': {
    };
    'massif::VectorElementSearchService': {
        'dataSource.dataExtent': true;
        'dataSource.projection.bounds': true;
    };
    'massif::VectorLayer': {
        'dataSource.dataExtent': true;
        'dataSource.projection.bounds': true;
    };
    'massif::VectorTileClickInfo': {
        'clickPos': true;
        'feature.geometry.bounds': true;
        'feature.geometry.centerPos': true;
        'featureClickPos': true;
        'featurePos': true;
    };
    'massif::VectorTileDecoder': {
    };
    'massif::VectorTileEventListener': {
    };
    'massif::VectorTileFeature': {
        'geometry.bounds': true;
        'geometry.centerPos': true;
    };
    'massif::VectorTileFeatureBuilder': {
        'geometry.bounds': true;
        'geometry.centerPos': true;
    };
    'massif::VectorTileFeatureCollection': {
    };
    'massif::VectorTileLayer': {
        'UTFGridDataSource.dataExtent': true;
        'UTFGridDataSource.projection.bounds': true;
        'dataSource.dataExtent': true;
        'dataSource.projection.bounds': true;
        'projection.bounds': true;
        'source.dataExtent': true;
        'source.projection.bounds': true;
    };
    'massif::VectorTileSearchService': {
        'dataSource.dataExtent': true;
        'dataSource.projection.bounds': true;
        'projection.bounds': true;
    };
    'massif::ViewState': {
    };
    'massif::WKBGeometryReader': {
    };
    'massif::WKBGeometryWriter': {
    };
    'massif::WKTGeometryReader': {
    };
    'massif::WKTGeometryWriter': {
    };
    'massif::ZippedAssetPackage': {
    };
}

/** @internal Per class, the paths that point at another object - see ObjectPath. */
export interface ObjectPaths {
    'massif::Address': {
    };
    'massif::AnimationStyle': {
    };
    'massif::AnimationStyleBuilder': {
    };
    'massif::AssetPackage': {
    };
    'massif::AssetTileDataSource': {
        'projection': 'massif::Projection';
    };
    'massif::BalloonPopup': {
        'balloonPopupEventListener': 'massif::BalloonPopupEventListener';
        'baseBillboard': 'massif::Billboard';
        'baseBillboard.baseBillboard': 'massif::Billboard';
        'baseBillboard.geometry': 'massif::Geometry';
        'baseBillboard.rootGeometry': 'massif::Geometry';
        'geometry': 'massif::Geometry';
        'rootGeometry': 'massif::Geometry';
        'style': 'massif::BalloonPopupStyle';
        'style.animationStyle': 'massif::AnimationStyle';
        'style.leftImage': 'massif::Bitmap';
        'style.rightImage': 'massif::Bitmap';
    };
    'massif::BalloonPopupButton': {
        'style': 'massif::BalloonPopupButtonStyle';
    };
    'massif::BalloonPopupButtonClickInfo': {
        'button': 'massif::BalloonPopupButton';
        'button.style': 'massif::BalloonPopupButtonStyle';
        'vectorElement': 'massif::VectorElement';
        'vectorElement.geometry': 'massif::Geometry';
    };
    'massif::BalloonPopupButtonStyle': {
    };
    'massif::BalloonPopupButtonStyleBuilder': {
    };
    'massif::BalloonPopupEventListener': {
    };
    'massif::BalloonPopupMargins': {
    };
    'massif::BalloonPopupStyle': {
        'animationStyle': 'massif::AnimationStyle';
        'leftImage': 'massif::Bitmap';
        'rightImage': 'massif::Bitmap';
    };
    'massif::BalloonPopupStyleBuilder': {
        'animationStyle': 'massif::AnimationStyle';
        'leftImage': 'massif::Bitmap';
        'rightImage': 'massif::Bitmap';
    };
    'massif::BaseMapView': {
        'mapRenderer': 'massif::MapRenderer';
        'mapRenderer.mapRendererListener': 'massif::MapRendererListener';
        'mapRenderer.postProcessEffect': 'massif::PostProcessEffect';
    };
    'massif::Billboard': {
        'baseBillboard': 'massif::Billboard';
        'geometry': 'massif::Geometry';
        'rootGeometry': 'massif::Geometry';
    };
    'massif::BillboardStyle': {
        'animationStyle': 'massif::AnimationStyle';
    };
    'massif::BillboardStyleBuilder': {
        'animationStyle': 'massif::AnimationStyle';
    };
    'massif::BinaryData': {
    };
    'massif::Bitmap': {
    };
    'massif::BitmapOverlayRasterTileDataSource': {
        'projection': 'massif::Projection';
    };
    'massif::BundleAssetPackage': {
    };
    'massif::CacheTileDataSource': {
        'dataSource': 'massif::TileDataSource';
        'dataSource.projection': 'massif::Projection';
        'projection': 'massif::Projection';
    };
    'massif::CartoCSSStyleSet': {
        'assetPackage': 'massif::AssetPackage';
    };
    'massif::CelestialArc': {
    };
    'massif::CelestialClickInfo': {
        'celestialObject': 'massif::CelestialObject';
    };
    'massif::CelestialEventListener': {
    };
    'massif::CelestialLabel': {
    };
    'massif::CelestialLayer': {
        'celestialEventListener': 'massif::CelestialEventListener';
    };
    'massif::CelestialObject': {
    };
    'massif::CelestialSprite': {
        'bitmap': 'massif::Bitmap';
    };
    'massif::ClickInfo': {
    };
    'massif::ClusterElementBuilder': {
    };
    'massif::ClusterFetchTask': {
    };
    'massif::ClusteredVectorLayer': {
        'clusterElementBuilder': 'massif::ClusterElementBuilder';
        'dataSource': 'massif::VectorDataSource';
        'dataSource.projection': 'massif::Projection';
        'vectorElementEventListener': 'massif::VectorElementEventListener';
    };
    'massif::Color': {
    };
    'massif::CombinedTileDataSource': {
        'projection': 'massif::Projection';
    };
    'massif::CompiledStyleSet': {
        'assetPackage': 'massif::AssetPackage';
    };
    'massif::CompositeVectorTileLayer': {
        'UTFGridDataSource': 'massif::TileDataSource';
        'UTFGridDataSource.projection': 'massif::Projection';
        'UTFGridEventListener': 'massif::UTFGridEventListener';
        'dataSource': 'massif::TileDataSource';
        'dataSource.projection': 'massif::Projection';
        'projection': 'massif::Projection';
        'source': 'massif::TileDataSource';
        'source.projection': 'massif::Projection';
        'style': 'massif::VectorTileDecoder';
        'tileDecoder': 'massif::VectorTileDecoder';
        'tileLoadListener': 'massif::TileLoadListener';
        'vectorTileEventListener': 'massif::VectorTileEventListener';
    };
    'massif::ContourTileDataSource': {
        'projection': 'massif::Projection';
        'terrainOptions': 'massif::TerrainOptions';
    };
    'massif::CullState': {
    };
    'massif::CustomPopup': {
        'baseBillboard': 'massif::Billboard';
        'baseBillboard.baseBillboard': 'massif::Billboard';
        'baseBillboard.geometry': 'massif::Geometry';
        'baseBillboard.rootGeometry': 'massif::Geometry';
        'geometry': 'massif::Geometry';
        'popupHandler': 'massif::CustomPopupHandler';
        'rootGeometry': 'massif::Geometry';
        'style': 'massif::PopupStyle';
        'style.animationStyle': 'massif::AnimationStyle';
    };
    'massif::CustomPopupHandler': {
    };
    'massif::CustomRasterTileLayer': {
        'UTFGridDataSource': 'massif::TileDataSource';
        'UTFGridDataSource.projection': 'massif::Projection';
        'UTFGridEventListener': 'massif::UTFGridEventListener';
        'dataSource': 'massif::TileDataSource';
        'dataSource.projection': 'massif::Projection';
        'projection': 'massif::Projection';
        'rasterTileEventListener': 'massif::RasterTileEventListener';
        'source': 'massif::TileDataSource';
        'source.projection': 'massif::Projection';
        'tileLoadListener': 'massif::TileLoadListener';
    };
    'massif::DataSourceListener': {
    };
    'massif::DirAssetPackage': {
    };
    'massif::DouglasPeuckerGeometrySimplifier': {
    };
    'massif::DownloadTask': {
    };
    'massif::EPSG3857': {
    };
    'massif::EPSG4326': {
    };
    'massif::EditableVectorLayer': {
        'dataSource': 'massif::VectorDataSource';
        'dataSource.projection': 'massif::Projection';
        'selectedVectorElement': 'massif::VectorElement';
        'selectedVectorElement.geometry': 'massif::Geometry';
        'vectorEditEventListener': 'massif::VectorEditEventListener';
        'vectorElementEventListener': 'massif::VectorElementEventListener';
    };
    'massif::ElevationDecoder': {
    };
    'massif::EventListener': {
    };
    'massif::Feature': {
        'geometry': 'massif::Geometry';
    };
    'massif::FeatureBuilder': {
        'geometry': 'massif::Geometry';
    };
    'massif::FeatureCollection': {
    };
    'massif::FeatureCollectionSearchService': {
        'featureCollection': 'massif::FeatureCollection';
        'projection': 'massif::Projection';
    };
    'massif::FetchTask': {
    };
    'massif::FetchTaskBase': {
    };
    'massif::FetchingTasks': {
    };
    'massif::FetchingTileTasks': {
    };
    'massif::FogOptions': {
    };
    'massif::GeoJSONGeometryReader': {
        'targetProjection': 'massif::Projection';
    };
    'massif::GeoJSONGeometryWriter': {
        'sourceProjection': 'massif::Projection';
    };
    'massif::GeoJSONVectorTileDataSource': {
        'projection': 'massif::Projection';
    };
    'massif::GeocodingAddress': {
    };
    'massif::GeocodingRequest': {
        'projection': 'massif::Projection';
    };
    'massif::GeocodingResult': {
        'featureCollection': 'massif::FeatureCollection';
        'projection': 'massif::Projection';
    };
    'massif::GeocodingService': {
    };
    'massif::Geometry': {
    };
    'massif::GeometryCollection': {
        'geometry': 'massif::MultiGeometry';
        'style': 'massif::GeometryCollectionStyle';
        'style.lineStyle': 'massif::LineStyle';
        'style.lineStyle.bitmap': 'massif::Bitmap';
        'style.pointStyle': 'massif::PointStyle';
        'style.pointStyle.bitmap': 'massif::Bitmap';
        'style.polygonStyle': 'massif::PolygonStyle';
        'style.polygonStyle.lineStyle': 'massif::LineStyle';
    };
    'massif::GeometryCollectionStyle': {
        'lineStyle': 'massif::LineStyle';
        'lineStyle.bitmap': 'massif::Bitmap';
        'pointStyle': 'massif::PointStyle';
        'pointStyle.bitmap': 'massif::Bitmap';
        'polygonStyle': 'massif::PolygonStyle';
        'polygonStyle.lineStyle': 'massif::LineStyle';
        'polygonStyle.lineStyle.bitmap': 'massif::Bitmap';
    };
    'massif::GeometryCollectionStyleBuilder': {
        'lineStyle': 'massif::LineStyle';
        'lineStyle.bitmap': 'massif::Bitmap';
        'pointStyle': 'massif::PointStyle';
        'pointStyle.bitmap': 'massif::Bitmap';
        'polygonStyle': 'massif::PolygonStyle';
        'polygonStyle.lineStyle': 'massif::LineStyle';
        'polygonStyle.lineStyle.bitmap': 'massif::Bitmap';
    };
    'massif::GeometrySimplifier': {
    };
    'massif::HTTPTileDataSource': {
        'projection': 'massif::Projection';
    };
    'massif::HillshadeRasterTileLayer': {
        'UTFGridDataSource': 'massif::TileDataSource';
        'UTFGridDataSource.projection': 'massif::Projection';
        'UTFGridEventListener': 'massif::UTFGridEventListener';
        'dataSource': 'massif::TileDataSource';
        'dataSource.projection': 'massif::Projection';
        'projection': 'massif::Projection';
        'rasterTileEventListener': 'massif::RasterTileEventListener';
        'source': 'massif::TileDataSource';
        'source.projection': 'massif::Projection';
        'tileLoadListener': 'massif::TileLoadListener';
    };
    'massif::Label': {
        'baseBillboard': 'massif::Billboard';
        'baseBillboard.baseBillboard': 'massif::Billboard';
        'baseBillboard.geometry': 'massif::Geometry';
        'baseBillboard.rootGeometry': 'massif::Geometry';
        'geometry': 'massif::Geometry';
        'rootGeometry': 'massif::Geometry';
        'style': 'massif::LabelStyle';
        'style.animationStyle': 'massif::AnimationStyle';
    };
    'massif::LabelStyle': {
        'animationStyle': 'massif::AnimationStyle';
    };
    'massif::LabelStyleBuilder': {
        'animationStyle': 'massif::AnimationStyle';
    };
    'massif::Layer': {
    };
    'massif::Layers': {
    };
    'massif::LightOptions': {
    };
    'massif::LightStop': {
    };
    'massif::Line': {
        'geometry': 'massif::LineGeometry';
        'style': 'massif::LineStyle';
        'style.bitmap': 'massif::Bitmap';
    };
    'massif::LineGeometry': {
    };
    'massif::LineStyle': {
        'bitmap': 'massif::Bitmap';
    };
    'massif::LineStyleBuilder': {
        'bitmap': 'massif::Bitmap';
    };
    'massif::LocalVectorDataSource': {
        'geometrySimplifier': 'massif::GeometrySimplifier';
        'projection': 'massif::Projection';
    };
    'massif::Log': {
        'logEventListener': 'massif::LogEventListener';
    };
    'massif::LogEventListener': {
    };
    'massif::MBTilesTileDataSource': {
        'projection': 'massif::Projection';
    };
    'massif::MBVectorTileDecoder': {
        'cartoCSSStyle': 'massif::CartoCSSStyleSet';
        'cartoCSSStyle.assetPackage': 'massif::AssetPackage';
        'compiledStyle': 'massif::CompiledStyleSet';
        'compiledStyle.assetPackage': 'massif::AssetPackage';
    };
    'massif::ManeuverArrowBuilder': {
    };
    'massif::MapBounds': {
    };
    'massif::MapBoxElevationDataDecoder': {
    };
    'massif::MapBoxOnlineGeocodingService': {
    };
    'massif::MapBoxOnlineReverseGeocodingService': {
    };
    'massif::MapClickInfo': {
    };
    'massif::MapEnvelope': {
    };
    'massif::MapEventListener': {
    };
    'massif::MapInteractionInfo': {
    };
    'massif::MapMoveInfo': {
    };
    'massif::MapPos': {
    };
    'massif::MapRange': {
    };
    'massif::MapRenderer': {
        'mapRendererListener': 'massif::MapRendererListener';
        'postProcessEffect': 'massif::PostProcessEffect';
    };
    'massif::MapRendererListener': {
    };
    'massif::MapTile': {
    };
    'massif::MapTilerOnlineTileDataSource': {
        'projection': 'massif::Projection';
    };
    'massif::MapVec': {
    };
    'massif::Marker': {
        'baseBillboard': 'massif::Billboard';
        'baseBillboard.baseBillboard': 'massif::Billboard';
        'baseBillboard.geometry': 'massif::Geometry';
        'baseBillboard.rootGeometry': 'massif::Geometry';
        'geometry': 'massif::Geometry';
        'rootGeometry': 'massif::Geometry';
        'style': 'massif::MarkerStyle';
        'style.animationStyle': 'massif::AnimationStyle';
        'style.bitmap': 'massif::Bitmap';
    };
    'massif::MarkerStyle': {
        'animationStyle': 'massif::AnimationStyle';
        'bitmap': 'massif::Bitmap';
    };
    'massif::MarkerStyleBuilder': {
        'animationStyle': 'massif::AnimationStyle';
        'bitmap': 'massif::Bitmap';
    };
    'massif::MassifApi': {
    };
    'massif::MassifInterop': {
    };
    'massif::MemoryCacheTileDataSource': {
        'dataSource': 'massif::TileDataSource';
        'dataSource.projection': 'massif::Projection';
        'projection': 'massif::Projection';
    };
    'massif::MergedMBVTTileDataSource': {
        'projection': 'massif::Projection';
    };
    'massif::MultiGeometry': {
    };
    'massif::MultiLineGeometry': {
    };
    'massif::MultiOSMOfflineGeocodingService': {
    };
    'massif::MultiOSMOfflineReverseGeocodingService': {
    };
    'massif::MultiPointGeometry': {
    };
    'massif::MultiPolygonGeometry': {
    };
    'massif::MultiTileDataSource': {
        'projection': 'massif::Projection';
    };
    'massif::MultiValhallaOfflineRoutingService': {
    };
    'massif::NMLModel': {
        'baseBillboard': 'massif::Billboard';
        'baseBillboard.baseBillboard': 'massif::Billboard';
        'baseBillboard.geometry': 'massif::Geometry';
        'baseBillboard.rootGeometry': 'massif::Geometry';
        'geometry': 'massif::Geometry';
        'rootGeometry': 'massif::Geometry';
        'style': 'massif::NMLModelStyle';
        'style.animationStyle': 'massif::AnimationStyle';
        'style.modelAsset': 'massif::BinaryData';
    };
    'massif::NMLModelStyle': {
        'animationStyle': 'massif::AnimationStyle';
        'modelAsset': 'massif::BinaryData';
    };
    'massif::NMLModelStyleBuilder': {
        'animationStyle': 'massif::AnimationStyle';
        'modelAsset': 'massif::BinaryData';
    };
    'massif::OSMOfflineGeocodingService': {
    };
    'massif::OSMOfflineReverseGeocodingService': {
    };
    'massif::OSRMOfflineRoutingService': {
    };
    'massif::OnChangeListener': {
    };
    'massif::Options': {
        'background': 'massif::Bitmap';
        'backgroundBitmap': 'massif::Bitmap';
        'baseProjection': 'massif::Projection';
        'fog': 'massif::FogOptions';
        'fogOptions': 'massif::FogOptions';
        'light': 'massif::LightOptions';
        'lightOptions': 'massif::LightOptions';
        'projection': 'massif::Projection';
        'sky': 'massif::SkyOptions';
        'skyOptions': 'massif::SkyOptions';
        'terrain': 'massif::TerrainOptions';
        'terrainOptions': 'massif::TerrainOptions';
    };
    'massif::OptionsListener': {
    };
    'massif::OrderedTileDataSource': {
        'projection': 'massif::Projection';
    };
    'massif::PMTilesTileDataSource': {
        'projection': 'massif::Projection';
    };
    'massif::PackageInfo': {
        'metaInfo': 'massif::PackageMetaInfo';
        'tileMask': 'massif::PackageTileMask';
    };
    'massif::PackageManager': {
        'packageManagerListener': 'massif::PackageManagerListener';
        'serverPackageListMetaInfo': 'massif::PackageMetaInfo';
    };
    'massif::PackageManagerGeocodingService': {
    };
    'massif::PackageManagerListener': {
    };
    'massif::PackageManagerReverseGeocodingService': {
    };
    'massif::PackageManagerRoutingService': {
    };
    'massif::PackageManagerTileDataSource': {
        'packageManager': 'massif::PackageManager';
        'packageManager.packageManagerListener': 'massif::PackageManagerListener';
        'packageManager.serverPackageListMetaInfo': 'massif::PackageMetaInfo';
        'projection': 'massif::Projection';
    };
    'massif::PackageManagerValhallaRoutingService': {
    };
    'massif::PackageMetaInfo': {
    };
    'massif::PackageStatus': {
    };
    'massif::PackageTileMask': {
    };
    'massif::PeliasOnlineGeocodingService': {
    };
    'massif::PeliasOnlineReverseGeocodingService': {
    };
    'massif::PersistentCacheTileDataSource': {
        'dataSource': 'massif::TileDataSource';
        'dataSource.projection': 'massif::Projection';
        'projection': 'massif::Projection';
    };
    'massif::PersistentTaskQueue': {
    };
    'massif::Point': {
        'geometry': 'massif::PointGeometry';
        'style': 'massif::PointStyle';
        'style.bitmap': 'massif::Bitmap';
    };
    'massif::PointDetailTileDataSource': {
        'projection': 'massif::Projection';
    };
    'massif::PointGeometry': {
    };
    'massif::PointStyle': {
        'bitmap': 'massif::Bitmap';
    };
    'massif::PointStyleBuilder': {
        'bitmap': 'massif::Bitmap';
    };
    'massif::Polygon': {
        'geometry': 'massif::PolygonGeometry';
        'style': 'massif::PolygonStyle';
        'style.lineStyle': 'massif::LineStyle';
        'style.lineStyle.bitmap': 'massif::Bitmap';
    };
    'massif::Polygon3D': {
        'geometry': 'massif::PolygonGeometry';
        'style': 'massif::Polygon3DStyle';
    };
    'massif::Polygon3DStyle': {
    };
    'massif::Polygon3DStyleBuilder': {
    };
    'massif::PolygonGeometry': {
    };
    'massif::PolygonStyle': {
        'lineStyle': 'massif::LineStyle';
        'lineStyle.bitmap': 'massif::Bitmap';
    };
    'massif::PolygonStyleBuilder': {
        'lineStyle': 'massif::LineStyle';
        'lineStyle.bitmap': 'massif::Bitmap';
    };
    'massif::Popup': {
        'baseBillboard': 'massif::Billboard';
        'baseBillboard.baseBillboard': 'massif::Billboard';
        'baseBillboard.geometry': 'massif::Geometry';
        'baseBillboard.rootGeometry': 'massif::Geometry';
        'geometry': 'massif::Geometry';
        'rootGeometry': 'massif::Geometry';
        'style': 'massif::PopupStyle';
        'style.animationStyle': 'massif::AnimationStyle';
    };
    'massif::PopupClickInfo': {
        'popup': 'massif::Popup';
        'popup.baseBillboard': 'massif::Billboard';
        'popup.baseBillboard.baseBillboard': 'massif::Billboard';
        'popup.baseBillboard.geometry': 'massif::Geometry';
        'popup.baseBillboard.rootGeometry': 'massif::Geometry';
        'popup.geometry': 'massif::Geometry';
        'popup.rootGeometry': 'massif::Geometry';
        'popup.style': 'massif::PopupStyle';
        'popup.style.animationStyle': 'massif::AnimationStyle';
    };
    'massif::PopupDrawInfo': {
        'popup': 'massif::Popup';
        'popup.baseBillboard': 'massif::Billboard';
        'popup.baseBillboard.baseBillboard': 'massif::Billboard';
        'popup.baseBillboard.geometry': 'massif::Geometry';
        'popup.baseBillboard.rootGeometry': 'massif::Geometry';
        'popup.geometry': 'massif::Geometry';
        'popup.rootGeometry': 'massif::Geometry';
        'popup.style': 'massif::PopupStyle';
        'popup.style.animationStyle': 'massif::AnimationStyle';
    };
    'massif::PopupStyle': {
        'animationStyle': 'massif::AnimationStyle';
    };
    'massif::PopupStyleBuilder': {
        'animationStyle': 'massif::AnimationStyle';
    };
    'massif::PostProcessEffect': {
    };
    'massif::Projection': {
    };
    'massif::RasterTileClickInfo': {
        'layer': 'massif::Layer';
    };
    'massif::RasterTileEventListener': {
    };
    'massif::RasterTileLayer': {
        'UTFGridDataSource': 'massif::TileDataSource';
        'UTFGridDataSource.projection': 'massif::Projection';
        'UTFGridEventListener': 'massif::UTFGridEventListener';
        'dataSource': 'massif::TileDataSource';
        'dataSource.projection': 'massif::Projection';
        'projection': 'massif::Projection';
        'rasterTileEventListener': 'massif::RasterTileEventListener';
        'source': 'massif::TileDataSource';
        'source.projection': 'massif::Projection';
        'tileLoadListener': 'massif::TileLoadListener';
    };
    'massif::RedrawRequestListener': {
    };
    'massif::RendererCaptureListener': {
    };
    'massif::ReverseGeocodingRequest': {
        'projection': 'massif::Projection';
    };
    'massif::ReverseGeocodingService': {
    };
    'massif::RouteMatchingEdge': {
    };
    'massif::RouteMatchingPoint': {
    };
    'massif::RouteMatchingRequest': {
        'projection': 'massif::Projection';
    };
    'massif::RouteMatchingResult': {
        'projection': 'massif::Projection';
    };
    'massif::RoutingInstruction': {
    };
    'massif::RoutingRequest': {
        'projection': 'massif::Projection';
    };
    'massif::RoutingResult': {
        'projection': 'massif::Projection';
    };
    'massif::RoutingService': {
    };
    'massif::SGREOfflineRoutingService': {
    };
    'massif::ScreenBounds': {
    };
    'massif::ScreenPos': {
    };
    'massif::SearchRequest': {
        'geometry': 'massif::Geometry';
        'projection': 'massif::Projection';
    };
    'massif::SkyOptions': {
    };
    'massif::SolidLayer': {
        'bitmap': 'massif::Bitmap';
    };
    'massif::Style': {
    };
    'massif::StyleBuilder': {
    };
    'massif::TerrainOptions': {
    };
    'massif::TerrariumElevationDataDecoder': {
    };
    'massif::Text': {
        'baseBillboard': 'massif::Billboard';
        'baseBillboard.baseBillboard': 'massif::Billboard';
        'baseBillboard.geometry': 'massif::Geometry';
        'baseBillboard.rootGeometry': 'massif::Geometry';
        'geometry': 'massif::Geometry';
        'rootGeometry': 'massif::Geometry';
        'style': 'massif::TextStyle';
        'style.animationStyle': 'massif::AnimationStyle';
    };
    'massif::TextMargins': {
    };
    'massif::TextStyle': {
        'animationStyle': 'massif::AnimationStyle';
    };
    'massif::TextStyleBuilder': {
        'animationStyle': 'massif::AnimationStyle';
    };
    'massif::TileData': {
        'data': 'massif::BinaryData';
    };
    'massif::TileDataSource': {
        'projection': 'massif::Projection';
    };
    'massif::TileDecoderListener': {
    };
    'massif::TileDownloadInfo': {
    };
    'massif::TileDownloadListener': {
    };
    'massif::TileInfo': {
    };
    'massif::TileLayer': {
        'UTFGridDataSource': 'massif::TileDataSource';
        'UTFGridDataSource.projection': 'massif::Projection';
        'UTFGridEventListener': 'massif::UTFGridEventListener';
        'dataSource': 'massif::TileDataSource';
        'dataSource.projection': 'massif::Projection';
        'projection': 'massif::Projection';
        'source': 'massif::TileDataSource';
        'source.projection': 'massif::Projection';
        'tileLoadListener': 'massif::TileLoadListener';
    };
    'massif::TileLoadListener': {
    };
    'massif::TileUtils': {
    };
    'massif::TomTomOnlineGeocodingService': {
    };
    'massif::TomTomOnlineReverseGeocodingService': {
    };
    'massif::TorqueTileDecoder': {
        'styleSet': 'massif::CartoCSSStyleSet';
        'styleSet.assetPackage': 'massif::AssetPackage';
    };
    'massif::TorqueTileLayer': {
        'UTFGridDataSource': 'massif::TileDataSource';
        'UTFGridDataSource.projection': 'massif::Projection';
        'UTFGridEventListener': 'massif::UTFGridEventListener';
        'dataSource': 'massif::TileDataSource';
        'dataSource.projection': 'massif::Projection';
        'projection': 'massif::Projection';
        'source': 'massif::TileDataSource';
        'source.projection': 'massif::Projection';
        'style': 'massif::VectorTileDecoder';
        'tileDecoder': 'massif::VectorTileDecoder';
        'tileLoadListener': 'massif::TileLoadListener';
        'vectorTileEventListener': 'massif::VectorTileEventListener';
    };
    'massif::TouchHandlerListener': {
    };
    'massif::UTFGridClickInfo': {
        'layer': 'massif::Layer';
    };
    'massif::UTFGridEventListener': {
    };
    'massif::UiDispatcher': {
    };
    'massif::ValhallaOfflineRoutingService': {
    };
    'massif::ValhallaOnlineRoutingService': {
    };
    'massif::Variant': {
    };
    'massif::VariantArrayBuilder': {
    };
    'massif::VariantObjectBuilder': {
    };
    'massif::VectorData': {
    };
    'massif::VectorDataSource': {
        'projection': 'massif::Projection';
    };
    'massif::VectorEditEventListener': {
    };
    'massif::VectorElement': {
        'geometry': 'massif::Geometry';
    };
    'massif::VectorElementClickInfo': {
        'layer': 'massif::Layer';
        'vectorElement': 'massif::VectorElement';
        'vectorElement.geometry': 'massif::Geometry';
    };
    'massif::VectorElementDragInfo': {
        'vectorElement': 'massif::VectorElement';
        'vectorElement.geometry': 'massif::Geometry';
    };
    'massif::VectorElementEventListener': {
    };
    'massif::VectorElementSearchService': {
        'dataSource': 'massif::VectorDataSource';
        'dataSource.projection': 'massif::Projection';
    };
    'massif::VectorLayer': {
        'dataSource': 'massif::VectorDataSource';
        'dataSource.projection': 'massif::Projection';
        'vectorElementEventListener': 'massif::VectorElementEventListener';
    };
    'massif::VectorTileClickInfo': {
        'feature': 'massif::VectorTileFeature';
        'feature.geometry': 'massif::Geometry';
        'layer': 'massif::Layer';
    };
    'massif::VectorTileDecoder': {
    };
    'massif::VectorTileEventListener': {
    };
    'massif::VectorTileFeature': {
        'geometry': 'massif::Geometry';
    };
    'massif::VectorTileFeatureBuilder': {
        'geometry': 'massif::Geometry';
    };
    'massif::VectorTileFeatureCollection': {
    };
    'massif::VectorTileLayer': {
        'UTFGridDataSource': 'massif::TileDataSource';
        'UTFGridDataSource.projection': 'massif::Projection';
        'UTFGridEventListener': 'massif::UTFGridEventListener';
        'dataSource': 'massif::TileDataSource';
        'dataSource.projection': 'massif::Projection';
        'projection': 'massif::Projection';
        'source': 'massif::TileDataSource';
        'source.projection': 'massif::Projection';
        'style': 'massif::VectorTileDecoder';
        'tileDecoder': 'massif::VectorTileDecoder';
        'tileLoadListener': 'massif::TileLoadListener';
        'vectorTileEventListener': 'massif::VectorTileEventListener';
    };
    'massif::VectorTileSearchService': {
        'dataSource': 'massif::TileDataSource';
        'dataSource.projection': 'massif::Projection';
        'projection': 'massif::Projection';
        'tileDecoder': 'massif::VectorTileDecoder';
    };
    'massif::ViewState': {
    };
    'massif::WKBGeometryReader': {
    };
    'massif::WKBGeometryWriter': {
    };
    'massif::WKTGeometryReader': {
    };
    'massif::WKTGeometryWriter': {
    };
    'massif::ZippedAssetPackage': {
    };
}

/** @internal Per class, the paths a dotted path may continue INTO - see ValuePath. */
export interface VariantPaths {
    'massif::Address': {
        'categories': true;
    };
    'massif::AnimationStyle': {
    };
    'massif::AnimationStyleBuilder': {
    };
    'massif::AssetPackage': {
        'assetNames': true;
    };
    'massif::AssetTileDataSource': {
        'dataExtent': true;
        'metaData': true;
        'projection.bounds': true;
    };
    'massif::BalloonPopup': {
        'baseBillboard.bounds': true;
        'baseBillboard.geometry.bounds': true;
        'baseBillboard.geometry.centerPos': true;
        'baseBillboard.metaData': true;
        'baseBillboard.rootGeometry.bounds': true;
        'baseBillboard.rootGeometry.centerPos': true;
        'bounds': true;
        'geometry.bounds': true;
        'geometry.centerPos': true;
        'metaData': true;
        'rootGeometry.bounds': true;
        'rootGeometry.centerPos': true;
        'style.buttonMargins': true;
        'style.descriptionMargins': true;
        'style.leftMargins': true;
        'style.rightMargins': true;
        'style.titleMargins': true;
    };
    'massif::BalloonPopupButton': {
        'style.textMargins': true;
        'tag': true;
    };
    'massif::BalloonPopupButtonClickInfo': {
        'button.style.textMargins': true;
        'button.tag': true;
        'clickInfo': true;
        'vectorElement.bounds': true;
        'vectorElement.geometry.bounds': true;
        'vectorElement.geometry.centerPos': true;
        'vectorElement.metaData': true;
    };
    'massif::BalloonPopupButtonStyle': {
        'textMargins': true;
    };
    'massif::BalloonPopupButtonStyleBuilder': {
        'textMargins': true;
    };
    'massif::BalloonPopupEventListener': {
    };
    'massif::BalloonPopupMargins': {
    };
    'massif::BalloonPopupStyle': {
        'buttonMargins': true;
        'descriptionMargins': true;
        'leftMargins': true;
        'rightMargins': true;
        'titleMargins': true;
    };
    'massif::BalloonPopupStyleBuilder': {
        'buttonMargins': true;
        'descriptionMargins': true;
        'leftMargins': true;
        'rightMargins': true;
        'titleMargins': true;
    };
    'massif::BaseMapView': {
        'cameraPos': true;
        'focusPos': true;
    };
    'massif::Billboard': {
        'bounds': true;
        'geometry.bounds': true;
        'geometry.centerPos': true;
        'metaData': true;
        'rootGeometry.bounds': true;
        'rootGeometry.centerPos': true;
    };
    'massif::BillboardStyle': {
    };
    'massif::BillboardStyleBuilder': {
    };
    'massif::BinaryData': {
    };
    'massif::Bitmap': {
    };
    'massif::BitmapOverlayRasterTileDataSource': {
        'dataExtent': true;
        'metaData': true;
        'projection.bounds': true;
    };
    'massif::BundleAssetPackage': {
        'assetNames': true;
        'localAssetNames': true;
    };
    'massif::CacheTileDataSource': {
        'dataExtent': true;
        'dataSource.dataExtent': true;
        'dataSource.metaData': true;
        'dataSource.projection.bounds': true;
        'metaData': true;
        'projection.bounds': true;
    };
    'massif::CartoCSSStyleSet': {
        'assetPackage.assetNames': true;
    };
    'massif::CelestialArc': {
        'metaData': true;
        'position': true;
    };
    'massif::CelestialClickInfo': {
        'celestialObject.metaData': true;
        'celestialObject.position': true;
        'clickInfo': true;
    };
    'massif::CelestialEventListener': {
    };
    'massif::CelestialLabel': {
        'metaData': true;
        'position': true;
    };
    'massif::CelestialLayer': {
        'metaData': true;
        'visibleZoomRange': true;
    };
    'massif::CelestialObject': {
        'metaData': true;
        'position': true;
    };
    'massif::CelestialSprite': {
        'metaData': true;
        'position': true;
    };
    'massif::ClickInfo': {
    };
    'massif::ClusterElementBuilder': {
    };
    'massif::ClusterFetchTask': {
    };
    'massif::ClusteredVectorLayer': {
        'dataSource.dataExtent': true;
        'dataSource.projection.bounds': true;
        'metaData': true;
        'visibleZoomRange': true;
    };
    'massif::Color': {
    };
    'massif::CombinedTileDataSource': {
        'dataExtent': true;
        'metaData': true;
        'projection.bounds': true;
    };
    'massif::CompiledStyleSet': {
        'assetPackage.assetNames': true;
    };
    'massif::CompositeVectorTileLayer': {
        'UTFGridDataSource.dataExtent': true;
        'UTFGridDataSource.metaData': true;
        'UTFGridDataSource.projection.bounds': true;
        'dataSource.dataExtent': true;
        'dataSource.metaData': true;
        'dataSource.projection.bounds': true;
        'metaData': true;
        'projection.bounds': true;
        'source.dataExtent': true;
        'source.metaData': true;
        'source.projection.bounds': true;
        'visibleZoomRange': true;
    };
    'massif::ContourTileDataSource': {
        'dataExtent': true;
        'metaData': true;
        'projection.bounds': true;
    };
    'massif::CullState': {
        'viewState': true;
    };
    'massif::CustomPopup': {
        'baseBillboard.bounds': true;
        'baseBillboard.geometry.bounds': true;
        'baseBillboard.geometry.centerPos': true;
        'baseBillboard.metaData': true;
        'baseBillboard.rootGeometry.bounds': true;
        'baseBillboard.rootGeometry.centerPos': true;
        'bounds': true;
        'geometry.bounds': true;
        'geometry.centerPos': true;
        'metaData': true;
        'rootGeometry.bounds': true;
        'rootGeometry.centerPos': true;
    };
    'massif::CustomPopupHandler': {
    };
    'massif::CustomRasterTileLayer': {
        'UTFGridDataSource.dataExtent': true;
        'UTFGridDataSource.metaData': true;
        'UTFGridDataSource.projection.bounds': true;
        'dataSource.dataExtent': true;
        'dataSource.metaData': true;
        'dataSource.projection.bounds': true;
        'metaData': true;
        'projection.bounds': true;
        'source.dataExtent': true;
        'source.metaData': true;
        'source.projection.bounds': true;
        'visibleZoomRange': true;
    };
    'massif::DataSourceListener': {
    };
    'massif::DirAssetPackage': {
        'assetNames': true;
        'localAssetNames': true;
    };
    'massif::DouglasPeuckerGeometrySimplifier': {
    };
    'massif::DownloadTask': {
    };
    'massif::EPSG3857': {
        'bounds': true;
    };
    'massif::EPSG4326': {
        'bounds': true;
    };
    'massif::EditableVectorLayer': {
        'dataSource.dataExtent': true;
        'dataSource.projection.bounds': true;
        'metaData': true;
        'selectedVectorElement.bounds': true;
        'selectedVectorElement.geometry.bounds': true;
        'selectedVectorElement.geometry.centerPos': true;
        'selectedVectorElement.metaData': true;
        'visibleZoomRange': true;
    };
    'massif::ElevationDecoder': {
    };
    'massif::EventListener': {
    };
    'massif::Feature': {
        'geometry.bounds': true;
        'geometry.centerPos': true;
        'properties': true;
    };
    'massif::FeatureBuilder': {
        'geometry.bounds': true;
        'geometry.centerPos': true;
    };
    'massif::FeatureCollection': {
    };
    'massif::FeatureCollectionSearchService': {
        'projection.bounds': true;
    };
    'massif::FetchTask': {
    };
    'massif::FetchTaskBase': {
    };
    'massif::FetchingTasks': {
    };
    'massif::FetchingTileTasks': {
    };
    'massif::FogOptions': {
    };
    'massif::GeoJSONGeometryReader': {
        'targetProjection.bounds': true;
    };
    'massif::GeoJSONGeometryWriter': {
        'sourceProjection.bounds': true;
    };
    'massif::GeoJSONVectorTileDataSource': {
        'dataExtent': true;
        'metaData': true;
        'projection.bounds': true;
    };
    'massif::GeocodingAddress': {
        'categories': true;
    };
    'massif::GeocodingRequest': {
        'location': true;
        'projection.bounds': true;
    };
    'massif::GeocodingResult': {
        'address': true;
        'projection.bounds': true;
    };
    'massif::GeocodingService': {
    };
    'massif::Geometry': {
        'bounds': true;
        'centerPos': true;
    };
    'massif::GeometryCollection': {
        'bounds': true;
        'geometry.bounds': true;
        'geometry.centerPos': true;
        'metaData': true;
    };
    'massif::GeometryCollectionStyle': {
    };
    'massif::GeometryCollectionStyleBuilder': {
    };
    'massif::GeometrySimplifier': {
    };
    'massif::HTTPTileDataSource': {
        'HTTPHeaders': true;
        'dataExtent': true;
        'metaData': true;
        'projection.bounds': true;
        'subdomains': true;
    };
    'massif::HillshadeRasterTileLayer': {
        'UTFGridDataSource.dataExtent': true;
        'UTFGridDataSource.metaData': true;
        'UTFGridDataSource.projection.bounds': true;
        'dataSource.dataExtent': true;
        'dataSource.metaData': true;
        'dataSource.projection.bounds': true;
        'illuminationDirection': true;
        'metaData': true;
        'projection.bounds': true;
        'source.dataExtent': true;
        'source.metaData': true;
        'source.projection.bounds': true;
        'visibleZoomRange': true;
    };
    'massif::Label': {
        'baseBillboard.bounds': true;
        'baseBillboard.geometry.bounds': true;
        'baseBillboard.geometry.centerPos': true;
        'baseBillboard.metaData': true;
        'baseBillboard.rootGeometry.bounds': true;
        'baseBillboard.rootGeometry.centerPos': true;
        'bounds': true;
        'geometry.bounds': true;
        'geometry.centerPos': true;
        'metaData': true;
        'rootGeometry.bounds': true;
        'rootGeometry.centerPos': true;
    };
    'massif::LabelStyle': {
    };
    'massif::LabelStyleBuilder': {
    };
    'massif::Layer': {
        'metaData': true;
        'visibleZoomRange': true;
    };
    'massif::Layers': {
    };
    'massif::LightOptions': {
        'dayCycleLightStops': true;
        'dayCycleRisingLightStops': true;
    };
    'massif::LightStop': {
    };
    'massif::Line': {
        'bounds': true;
        'geometry.bounds': true;
        'geometry.centerPos': true;
        'geometry.poses': true;
        'metaData': true;
    };
    'massif::LineGeometry': {
        'bounds': true;
        'centerPos': true;
        'poses': true;
    };
    'massif::LineStyle': {
    };
    'massif::LineStyleBuilder': {
    };
    'massif::LocalVectorDataSource': {
        'dataExtent': true;
        'projection.bounds': true;
    };
    'massif::Log': {
    };
    'massif::LogEventListener': {
    };
    'massif::MBTilesTileDataSource': {
        'dataExtent': true;
        'metaData': true;
        'projection.bounds': true;
    };
    'massif::MBVectorTileDecoder': {
        'cartoCSSStyle.assetPackage.assetNames': true;
        'compiledStyle.assetPackage.assetNames': true;
        'styleLayerNames': true;
        'styleParameters': true;
    };
    'massif::ManeuverArrowBuilder': {
    };
    'massif::MapBounds': {
        'center': true;
        'delta': true;
        'max': true;
        'min': true;
    };
    'massif::MapBoxElevationDataDecoder': {
    };
    'massif::MapBoxOnlineGeocodingService': {
    };
    'massif::MapBoxOnlineReverseGeocodingService': {
    };
    'massif::MapClickInfo': {
        'clickInfo': true;
        'clickPos': true;
    };
    'massif::MapEnvelope': {
        'bounds': true;
        'convexHull': true;
    };
    'massif::MapEventListener': {
    };
    'massif::MapInteractionInfo': {
    };
    'massif::MapMoveInfo': {
    };
    'massif::MapPos': {
    };
    'massif::MapRange': {
    };
    'massif::MapRenderer': {
    };
    'massif::MapRendererListener': {
    };
    'massif::MapTile': {
    };
    'massif::MapTilerOnlineTileDataSource': {
        'dataExtent': true;
        'metaData': true;
        'projection.bounds': true;
    };
    'massif::MapVec': {
        'normalized': true;
    };
    'massif::Marker': {
        'baseBillboard.bounds': true;
        'baseBillboard.geometry.bounds': true;
        'baseBillboard.geometry.centerPos': true;
        'baseBillboard.metaData': true;
        'baseBillboard.rootGeometry.bounds': true;
        'baseBillboard.rootGeometry.centerPos': true;
        'bounds': true;
        'geometry.bounds': true;
        'geometry.centerPos': true;
        'metaData': true;
        'rootGeometry.bounds': true;
        'rootGeometry.centerPos': true;
    };
    'massif::MarkerStyle': {
    };
    'massif::MarkerStyleBuilder': {
    };
    'massif::MassifApi': {
    };
    'massif::MassifInterop': {
    };
    'massif::MemoryCacheTileDataSource': {
        'dataExtent': true;
        'dataSource.dataExtent': true;
        'dataSource.metaData': true;
        'dataSource.projection.bounds': true;
        'metaData': true;
        'projection.bounds': true;
    };
    'massif::MergedMBVTTileDataSource': {
        'dataExtent': true;
        'metaData': true;
        'projection.bounds': true;
    };
    'massif::MultiGeometry': {
        'bounds': true;
        'centerPos': true;
    };
    'massif::MultiLineGeometry': {
        'bounds': true;
        'centerPos': true;
    };
    'massif::MultiOSMOfflineGeocodingService': {
    };
    'massif::MultiOSMOfflineReverseGeocodingService': {
    };
    'massif::MultiPointGeometry': {
        'bounds': true;
        'centerPos': true;
    };
    'massif::MultiPolygonGeometry': {
        'bounds': true;
        'centerPos': true;
    };
    'massif::MultiTileDataSource': {
        'dataExtent': true;
        'metaData': true;
        'projection.bounds': true;
    };
    'massif::MultiValhallaOfflineRoutingService': {
    };
    'massif::NMLModel': {
        'baseBillboard.bounds': true;
        'baseBillboard.geometry.bounds': true;
        'baseBillboard.geometry.centerPos': true;
        'baseBillboard.metaData': true;
        'baseBillboard.rootGeometry.bounds': true;
        'baseBillboard.rootGeometry.centerPos': true;
        'bounds': true;
        'geometry.bounds': true;
        'geometry.centerPos': true;
        'metaData': true;
        'rootGeometry.bounds': true;
        'rootGeometry.centerPos': true;
        'rotationAxis': true;
    };
    'massif::NMLModelStyle': {
    };
    'massif::NMLModelStyleBuilder': {
    };
    'massif::OSMOfflineGeocodingService': {
    };
    'massif::OSMOfflineReverseGeocodingService': {
    };
    'massif::OSRMOfflineRoutingService': {
    };
    'massif::OnChangeListener': {
    };
    'massif::Options': {
        'baseProjection.bounds': true;
        'focusPointOffset': true;
        'light.dayCycleLightStops': true;
        'light.dayCycleRisingLightStops': true;
        'lightOptions.dayCycleLightStops': true;
        'lightOptions.dayCycleRisingLightStops': true;
        'mainLightDirection': true;
        'panBounds': true;
        'projection.bounds': true;
        'tiltRange': true;
        'zoomRange': true;
    };
    'massif::OptionsListener': {
    };
    'massif::OrderedTileDataSource': {
        'dataExtent': true;
        'metaData': true;
        'projection.bounds': true;
    };
    'massif::PMTilesTileDataSource': {
        'dataExtent': true;
        'metaData': true;
        'projection.bounds': true;
    };
    'massif::PackageInfo': {
        'metaInfo.variant': true;
    };
    'massif::PackageManager': {
        'localPackages': true;
        'serverPackageListMetaInfo.variant': true;
        'serverPackages': true;
    };
    'massif::PackageManagerGeocodingService': {
    };
    'massif::PackageManagerListener': {
    };
    'massif::PackageManagerReverseGeocodingService': {
    };
    'massif::PackageManagerRoutingService': {
    };
    'massif::PackageManagerTileDataSource': {
        'dataExtent': true;
        'metaData': true;
        'packageManager.localPackages': true;
        'packageManager.serverPackageListMetaInfo.variant': true;
        'packageManager.serverPackages': true;
        'projection.bounds': true;
    };
    'massif::PackageManagerValhallaRoutingService': {
    };
    'massif::PackageMetaInfo': {
        'variant': true;
    };
    'massif::PackageStatus': {
    };
    'massif::PackageTileMask': {
    };
    'massif::PeliasOnlineGeocodingService': {
    };
    'massif::PeliasOnlineReverseGeocodingService': {
    };
    'massif::PersistentCacheTileDataSource': {
        'dataExtent': true;
        'dataSource.dataExtent': true;
        'dataSource.metaData': true;
        'dataSource.projection.bounds': true;
        'metaData': true;
        'projection.bounds': true;
    };
    'massif::PersistentTaskQueue': {
    };
    'massif::Point': {
        'bounds': true;
        'geometry.bounds': true;
        'geometry.centerPos': true;
        'geometry.pos': true;
        'metaData': true;
    };
    'massif::PointDetailTileDataSource': {
        'dataExtent': true;
        'metaData': true;
        'projection.bounds': true;
    };
    'massif::PointGeometry': {
        'bounds': true;
        'centerPos': true;
        'pos': true;
    };
    'massif::PointStyle': {
    };
    'massif::PointStyleBuilder': {
    };
    'massif::Polygon': {
        'bounds': true;
        'geometry.bounds': true;
        'geometry.centerPos': true;
        'geometry.holes': true;
        'geometry.poses': true;
        'geometry.rings': true;
        'metaData': true;
    };
    'massif::Polygon3D': {
        'bounds': true;
        'geometry.bounds': true;
        'geometry.centerPos': true;
        'geometry.holes': true;
        'geometry.poses': true;
        'geometry.rings': true;
        'metaData': true;
    };
    'massif::Polygon3DStyle': {
    };
    'massif::Polygon3DStyleBuilder': {
    };
    'massif::PolygonGeometry': {
        'bounds': true;
        'centerPos': true;
        'holes': true;
        'poses': true;
        'rings': true;
    };
    'massif::PolygonStyle': {
    };
    'massif::PolygonStyleBuilder': {
    };
    'massif::Popup': {
        'baseBillboard.bounds': true;
        'baseBillboard.geometry.bounds': true;
        'baseBillboard.geometry.centerPos': true;
        'baseBillboard.metaData': true;
        'baseBillboard.rootGeometry.bounds': true;
        'baseBillboard.rootGeometry.centerPos': true;
        'bounds': true;
        'geometry.bounds': true;
        'geometry.centerPos': true;
        'metaData': true;
        'rootGeometry.bounds': true;
        'rootGeometry.centerPos': true;
    };
    'massif::PopupClickInfo': {
        'clickInfo': true;
        'clickPos': true;
        'elementClickPos': true;
        'popup.baseBillboard.bounds': true;
        'popup.baseBillboard.metaData': true;
        'popup.bounds': true;
        'popup.geometry.bounds': true;
        'popup.geometry.centerPos': true;
        'popup.metaData': true;
        'popup.rootGeometry.bounds': true;
        'popup.rootGeometry.centerPos': true;
    };
    'massif::PopupDrawInfo': {
        'anchorScreenPos': true;
        'popup.baseBillboard.bounds': true;
        'popup.baseBillboard.metaData': true;
        'popup.bounds': true;
        'popup.geometry.bounds': true;
        'popup.geometry.centerPos': true;
        'popup.metaData': true;
        'popup.rootGeometry.bounds': true;
        'popup.rootGeometry.centerPos': true;
        'screenBounds': true;
    };
    'massif::PopupStyle': {
    };
    'massif::PopupStyleBuilder': {
    };
    'massif::PostProcessEffect': {
    };
    'massif::Projection': {
        'bounds': true;
    };
    'massif::RasterTileClickInfo': {
        'clickInfo': true;
        'clickPos': true;
        'layer.metaData': true;
        'layer.visibleZoomRange': true;
        'mapTile': true;
    };
    'massif::RasterTileEventListener': {
    };
    'massif::RasterTileLayer': {
        'UTFGridDataSource.dataExtent': true;
        'UTFGridDataSource.metaData': true;
        'UTFGridDataSource.projection.bounds': true;
        'dataSource.dataExtent': true;
        'dataSource.metaData': true;
        'dataSource.projection.bounds': true;
        'metaData': true;
        'projection.bounds': true;
        'source.dataExtent': true;
        'source.metaData': true;
        'source.projection.bounds': true;
        'visibleZoomRange': true;
    };
    'massif::RedrawRequestListener': {
    };
    'massif::RendererCaptureListener': {
    };
    'massif::ReverseGeocodingRequest': {
        'location': true;
        'projection.bounds': true;
    };
    'massif::ReverseGeocodingService': {
    };
    'massif::RouteMatchingEdge': {
    };
    'massif::RouteMatchingPoint': {
        'pos': true;
    };
    'massif::RouteMatchingRequest': {
        'points': true;
        'projection.bounds': true;
    };
    'massif::RouteMatchingResult': {
        'matchingEdges': true;
        'matchingPoints': true;
        'points': true;
        'projection.bounds': true;
    };
    'massif::RoutingInstruction': {
        'geometryTag': true;
    };
    'massif::RoutingRequest': {
        'points': true;
        'projection.bounds': true;
    };
    'massif::RoutingResult': {
        'instructions': true;
        'points': true;
        'projection.bounds': true;
    };
    'massif::RoutingService': {
    };
    'massif::SGREOfflineRoutingService': {
    };
    'massif::ScreenBounds': {
        'center': true;
        'max': true;
        'min': true;
    };
    'massif::ScreenPos': {
    };
    'massif::SearchRequest': {
        'geometry.bounds': true;
        'geometry.centerPos': true;
        'projection.bounds': true;
    };
    'massif::SkyOptions': {
    };
    'massif::SolidLayer': {
        'metaData': true;
        'visibleZoomRange': true;
    };
    'massif::Style': {
    };
    'massif::StyleBuilder': {
    };
    'massif::TerrainOptions': {
    };
    'massif::TerrariumElevationDataDecoder': {
    };
    'massif::Text': {
        'baseBillboard.bounds': true;
        'baseBillboard.geometry.bounds': true;
        'baseBillboard.geometry.centerPos': true;
        'baseBillboard.metaData': true;
        'baseBillboard.rootGeometry.bounds': true;
        'baseBillboard.rootGeometry.centerPos': true;
        'bounds': true;
        'geometry.bounds': true;
        'geometry.centerPos': true;
        'metaData': true;
        'rootGeometry.bounds': true;
        'rootGeometry.centerPos': true;
        'style.textMargins': true;
    };
    'massif::TextMargins': {
    };
    'massif::TextStyle': {
        'textMargins': true;
    };
    'massif::TextStyleBuilder': {
        'textMargins': true;
    };
    'massif::TileData': {
    };
    'massif::TileDataSource': {
        'dataExtent': true;
        'metaData': true;
        'projection.bounds': true;
    };
    'massif::TileDecoderListener': {
    };
    'massif::TileDownloadInfo': {
        'tile': true;
    };
    'massif::TileDownloadListener': {
    };
    'massif::TileInfo': {
    };
    'massif::TileLayer': {
        'UTFGridDataSource.dataExtent': true;
        'UTFGridDataSource.metaData': true;
        'UTFGridDataSource.projection.bounds': true;
        'dataSource.dataExtent': true;
        'dataSource.metaData': true;
        'dataSource.projection.bounds': true;
        'metaData': true;
        'projection.bounds': true;
        'source.dataExtent': true;
        'source.metaData': true;
        'source.projection.bounds': true;
        'visibleZoomRange': true;
    };
    'massif::TileLoadListener': {
    };
    'massif::TileUtils': {
    };
    'massif::TomTomOnlineGeocodingService': {
    };
    'massif::TomTomOnlineReverseGeocodingService': {
    };
    'massif::TorqueTileDecoder': {
        'styleSet.assetPackage.assetNames': true;
    };
    'massif::TorqueTileLayer': {
        'UTFGridDataSource.dataExtent': true;
        'UTFGridDataSource.metaData': true;
        'UTFGridDataSource.projection.bounds': true;
        'dataSource.dataExtent': true;
        'dataSource.metaData': true;
        'dataSource.projection.bounds': true;
        'metaData': true;
        'projection.bounds': true;
        'source.dataExtent': true;
        'source.metaData': true;
        'source.projection.bounds': true;
        'visibleZoomRange': true;
    };
    'massif::TouchHandlerListener': {
    };
    'massif::UTFGridClickInfo': {
        'clickInfo': true;
        'clickPos': true;
        'elementInfo': true;
        'layer.metaData': true;
        'layer.visibleZoomRange': true;
    };
    'massif::UTFGridEventListener': {
    };
    'massif::UiDispatcher': {
    };
    'massif::ValhallaOfflineRoutingService': {
    };
    'massif::ValhallaOnlineRoutingService': {
        'HTTPHeaders': true;
    };
    'massif::Variant': {
        'objectKeys': true;
    };
    'massif::VariantArrayBuilder': {
    };
    'massif::VariantObjectBuilder': {
    };
    'massif::VectorData': {
        'elements': true;
    };
    'massif::VectorDataSource': {
        'dataExtent': true;
        'projection.bounds': true;
    };
    'massif::VectorEditEventListener': {
    };
    'massif::VectorElement': {
        'bounds': true;
        'geometry.bounds': true;
        'geometry.centerPos': true;
        'metaData': true;
    };
    'massif::VectorElementClickInfo': {
        'clickInfo': true;
        'clickPos': true;
        'elementClickPos': true;
        'layer.metaData': true;
        'layer.visibleZoomRange': true;
        'vectorElement.bounds': true;
        'vectorElement.geometry.bounds': true;
        'vectorElement.geometry.centerPos': true;
        'vectorElement.metaData': true;
    };
    'massif::VectorElementDragInfo': {
        'mapPos': true;
        'screenPos': true;
        'vectorElement.bounds': true;
        'vectorElement.geometry.bounds': true;
        'vectorElement.geometry.centerPos': true;
        'vectorElement.metaData': true;
    };
    'massif::VectorElementEventListener': {
    };
    'massif::VectorElementSearchService': {
        'dataSource.dataExtent': true;
        'dataSource.projection.bounds': true;
    };
    'massif::VectorLayer': {
        'dataSource.dataExtent': true;
        'dataSource.projection.bounds': true;
        'metaData': true;
        'visibleZoomRange': true;
    };
    'massif::VectorTileClickInfo': {
        'clickInfo': true;
        'clickPos': true;
        'feature.geometry.bounds': true;
        'feature.geometry.centerPos': true;
        'feature.mapTile': true;
        'feature.properties': true;
        'featureClickPos': true;
        'featurePos': true;
        'layer.metaData': true;
        'layer.visibleZoomRange': true;
        'mapTile': true;
    };
    'massif::VectorTileDecoder': {
    };
    'massif::VectorTileEventListener': {
    };
    'massif::VectorTileFeature': {
        'geometry.bounds': true;
        'geometry.centerPos': true;
        'mapTile': true;
        'properties': true;
    };
    'massif::VectorTileFeatureBuilder': {
        'geometry.bounds': true;
        'geometry.centerPos': true;
        'mapTile': true;
    };
    'massif::VectorTileFeatureCollection': {
    };
    'massif::VectorTileLayer': {
        'UTFGridDataSource.dataExtent': true;
        'UTFGridDataSource.metaData': true;
        'UTFGridDataSource.projection.bounds': true;
        'dataSource.dataExtent': true;
        'dataSource.metaData': true;
        'dataSource.projection.bounds': true;
        'metaData': true;
        'projection.bounds': true;
        'source.dataExtent': true;
        'source.metaData': true;
        'source.projection.bounds': true;
        'visibleZoomRange': true;
    };
    'massif::VectorTileSearchService': {
        'dataSource.dataExtent': true;
        'dataSource.metaData': true;
        'dataSource.projection.bounds': true;
        'layers': true;
        'projection.bounds': true;
    };
    'massif::ViewState': {
    };
    'massif::WKBGeometryReader': {
    };
    'massif::WKBGeometryWriter': {
    };
    'massif::WKTGeometryReader': {
    };
    'massif::WKTGeometryWriter': {
    };
    'massif::ZippedAssetPackage': {
        'assetNames': true;
        'localAssetNames': true;
    };
}

/**
 * Whether `C` says nothing about the class.
 *
 * True for `any` - the default, and so what a bare `MassifLayer` or `MassifObject` carries -
 * and for the whole `ClassName` union, which is what `ClassAtPath` falls back to when the table
 * does not record the class at the end of an object path. Both mean the same thing here: the class
 * is not known at compile time, the C++ resolves against the runtime one, and a table lookup can
 * only produce a wrong answer.
 *
 * The usual `0 extends 1 & C` probe does NOT work: `C` is constrained to `ClassName`, and the
 * intersection resolves against that constraint rather than staying deferred.
 */
type Unnarrowed<C extends ClassName> = ClassName extends C ? true : false;

export type Path<C extends ClassName> = keyof PropertyTypes[C] & string;
/**
 * The type at the end of a path.
 *
 * Constrained to `Path | ValuePath` rather than to either alone: `set` writes object paths
 * too, and `get` reads paths that continue into free-form data, which is the template arm.
 */
export type ValueAt<C extends ClassName, P extends Path<C> | ValuePath<C>> = P extends Path<C> ? PropertyTypes[C][P] : Json;

/**
 * A spec written inline, where an OBJECT property is expected.
 *
 * `setObject` carries only a handle, so a spec used to collapse to NULL_HANDLE and CLEAR the
 * property while reporting success - which is what blanked `backgroundBitmap`. `set` builds it
 * now, so the type has to allow it.
 */
export type SpecValue = { type: string } & { [key: string]: any };

/**
 * The type `set` ACCEPTS at a path: what `get` returns, plus an inline spec wherever the
 * value is an object handle.
 */
export type WriteAt<C extends ClassName, P extends Path<C> | ValuePath<C>> =
    ValueAt<C, P> extends Handle ? Handle | SpecValue : ValueAt<C, P>;

/*
 * The paths that are NOT read-only.
 *
 * `readonly` on an interface member only stops direct assignment - it does nothing about
 * set(path, value), where the path is a string. This is the standard writable-keys probe: two
 * mapped types differing only in `readonly` are assignable to each other exactly when the key
 * is writable.
 */
type IfWritable<T, K extends keyof T, Yes, No> = (<G>() => G extends { [Q in K]: T[K] } ? 1 : 2) extends <G>() => G extends { readonly [Q in K]: T[K] } ? 1 : 2 ? No : Yes;

export type WritablePath<C extends ClassName> = {
    [K in keyof PropertyTypes[C]]-?: IfWritable<PropertyTypes[C], K, K, never>;
}[keyof PropertyTypes[C]] &
    string;

/** The paths the SDK flags as coordinates, so `getPos` can convert them to another projection. */
export type PositionPath<C extends ClassName> = keyof PositionPaths[C] & string;

/**
 * The coordinate at a position path: a `MapPos` reads as a `Position`, a `MapBounds` as a
 * `Bounds`. `PositionPaths` only records THAT a path is a coordinate; which of the two it is
 * comes from the property table, so a caller does not have to narrow `Position | Bounds` back
 * down by hand at every click handler.
 */
export type PositionAt<C extends ClassName, P extends PositionPath<C>> = Unnarrowed<C> extends true
    ? Position | Bounds
    : P extends Path<C>
      ? Extract<PropertyTypes[C][P], Position | Bounds>
      : Position | Bounds;

/** The paths that point at another object. `group` scopes onto one; `get` cannot read one. */
export type ObjectPath<C extends ClassName> = keyof ObjectPaths[C] & string;

/** The paths a dotted path may keep walking past - a Variant, or a struct's own JSON. */
export type VariantPath<C extends ClassName> = keyof VariantPaths[C] & string;

/**
 * The paths `get` can read.
 *
 * An object property is excluded on purpose: the facade has no getObject, and a handle to an
 * intermediate is not what a caller wants anyway - `group('fogOptions')` is.
 *
 * The template arm is free-form data: the C++ keeps walking inside a Variant, so
 * `feature.properties.name` resolves even though no table can know the leaf. Its type comes
 * back as `Json`, which is what it honestly is.
 *
 * An UNNARROWED object - `MassifLayer`, which is what `layers().get(i)` and `source()` hand
 * back - takes any path, the way `Path` and `WritablePath` already do: nothing is known about the
 * class, so nothing can be said about its paths, and the C++ resolves the path either way. Without
 * the guard, `Path<any>` and `ObjectPath<any>` were both `string`, their `Exclude` was
 * `never`, and only the dotted arm survived - so `get('maxZoom')` was an error on every object
 * whose class had not been named.
 */
export type ValuePath<C extends ClassName> = Unnarrowed<C> extends true ? string : Exclude<Path<C>, ObjectPath<C>> | `${VariantPath<C>}.${string}`;

/** The class an object property points at, so a scope onto it stays typed all the way down. */
export type ClassAtPath<C extends ClassName, P extends ObjectPath<C>> = ObjectPaths[C][P] extends ClassName ? ObjectPaths[C][P] : ClassName;

export type SpecType<K extends Kind> = keyof SpecClass[K] & string;

/**
 * The spec of one type, as a function parameter.
 *
 * Intersected rather than `Extract`ed on purpose: TypeScript loses object-literal freshness
 * through a naked type parameter, so `spec: S` with `S extends SpecOf[K]` accepts a misspelt
 * key silently. This shape keeps the excess-property check AND still infers `T` from `type`.
 */
export type SpecArg<K extends Kind, T extends SpecType<K>> = SpecOf[K] & { type: T };

/** The concrete class a `{ type: T }` spec of kind `K` builds, so its properties complete. */
export type ClassOfSpec<K extends Kind, T extends SpecType<K>> = SpecClass[K][T] extends ClassName ? SpecClass[K][T] : ClassName;

export type MethodName<C extends ClassName> = keyof MethodTypes[C] & string;
/**
 * A method's parameters, as a tuple.
 *
 * `unknown[]` for an unnarrowed object, for the same reason `ValuePath` takes any path there -
 * and because this one is spread as a REST parameter: resolving to `any` is not a rest type at
 * all, so `call('clearTileCaches', true)` reported its argument as `never` rather than accepting
 * anything.
 */
export type MethodArgs<C extends ClassName, M extends MethodName<C>> = Unnarrowed<C> extends true ? unknown[] : MethodTypes[C][M] extends { args: infer A } ? (A extends unknown[] ? A : never) : never;
export type MethodResult<C extends ClassName, M extends MethodName<C>> = MethodTypes[C][M] extends { result: infer R } ? R : never;

/** The class of an object result, or never for a scalar one. */
export type MethodResultClass<C extends ClassName, M extends MethodName<C>> = MethodTypes[C][M] extends { resultClass: infer R } ? (R extends ClassName ? R : never) : never;

export type EventName<C extends ClassName> = keyof EventTypes[C] & string;

/** The class of an event's payload, so a handler's reads complete against the right table. */
export type PayloadClass<C extends ClassName, E extends EventName<C>> = EventTypes[C][E] extends ClassName ? EventTypes[C][E] : never;

// --- specs ---------------------------------------------------------------
//
// A spec's keys are its constructor parameters plus any writable property of the
// class it builds - the factory consumes what it needs and the rest is applied as
// properties, at any nesting depth. That IS the complete set the C++ accepts, so
// there is no string index signature: a key not listed is one `create` would drop
// with a warning, and it is better said at compile time. Cast if you are writing a
// spec against a newer SDK than these typings were generated from.
//
// A key is REQUIRED when every constructor overload requires it - `create` fails
// with RESULT_BAD_SPEC when no overload is satisfied.

export interface AssetsSpec_bundle {
    type: 'bundle';
    base?: Handle | string | AssetsSpec;
    path: string;
}

export interface AssetsSpec_dir {
    type: 'dir';
    base?: Handle | string | AssetsSpec;
    path: string;
}

export interface AssetsSpec_zip {
    type: 'zip';
    base?: Handle | string | AssetsSpec;
    data: Handle | string | Record<string, Json>;
}

export type AssetsSpec = AssetsSpec_bundle | AssetsSpec_dir | AssetsSpec_zip;

export interface CelestialSpec_arc {
    type: 'arc';
    /** Returns whether the part of the curve below the horizon is drawn. */
    belowHorizonVisible?: boolean;
    /** Returns the click radius of the curve. */
    clickRadius?: number;
    /** Returns the color of the object. */
    color?: number;
    /** Returns a copy of the meta data map. Changes to the copy are not reflected in the object. */
    metaData?: Record<string, Json>;
    /** Returns whether the map in front hides the object. */
    occludedByMap?: boolean;
    /** Returns the visibility of the object. */
    visible?: boolean;
    /** Returns the line width. */
    width?: number;
}

export interface CelestialSpec_label {
    type: 'label';
    /** Returns the background colour. */
    backgroundColor?: number;
    /** Returns the corner radius of the plate. */
    backgroundRadius?: number;
    /** Returns whether a click on the label hits it. */
    clickable?: boolean;
    /** Returns the color of the object. */
    color?: number;
    /** Returns the font list. */
    fontName?: string;
    /** Returns the font size. */
    fontSize?: number;
    /** Returns the halo colour. */
    haloColor?: number;
    /** Returns the halo width. */
    haloWidth?: number;
    /** Returns a copy of the meta data map. Changes to the copy are not reflected in the object. */
    metaData?: Record<string, Json>;
    /** Returns whether the map in front hides the object. */
    occludedByMap?: boolean;
    /** Returns the horizontal padding between the text and the plate's edge. */
    paddingX?: number;
    /** Returns the vertical padding between the text and the plate's edge. */
    paddingY?: number;
    /** Returns the text. */
    text?: string;
    /** Returns the text colour. The object's own colour tints the whole label, plate included. */
    textColor?: number;
    /** Returns the visibility of the object. */
    visible?: boolean;
}

export interface CelestialSpec_sprite {
    type: 'sprite';
    /** Returns the angular size of the sprite. */
    angularSize?: number;
    /** Returns the bitmap of the sprite. */
    bitmap?: Handle | string | Record<string, Json>;
    /** Returns the extra radius that responds to a click. */
    clickRadius?: number;
    /** Returns the color of the object. */
    color?: number;
    /** Returns a copy of the meta data map. Changes to the copy are not reflected in the object. */
    metaData?: Record<string, Json>;
    /** Returns whether the map in front hides the object. */
    occludedByMap?: boolean;
    /** Returns the screen size of the sprite. */
    screenSize?: number;
    /** Returns the edge softness of a disc sprite. */
    softness?: number;
    /** Returns the visibility of the object. */
    visible?: boolean;
}

export type CelestialSpec = CelestialSpec_arc | CelestialSpec_label | CelestialSpec_sprite;

export interface EffectSpec_postprocess {
    type: 'postprocess';
    fragmentShader: string;
    name: string;
    /** Returns true if the effect needs the terrain depth pre-pass (uTerrainDepthTex). */
    terrainDepthRequired?: boolean;
    /** Returns true if the effect wants the terrain surface normal in the depth pre-pass. */
    terrainNormalsRequired?: boolean;
}

export type EffectSpec = EffectSpec_postprocess;

export interface ElementSpec_balloon {
    type: 'balloon';
    /** Returns the horizontal anchor point of this popup. */
    anchorPointX?: number;
    /** Returns the vertical anchor point of this popup. */
    anchorPointY?: number;
    /** Returns the balloon popup event listener. */
    balloonPopupEventListener?: Handle;
    /** Returns the base billboard this billboard is attached to. */
    baseBillboard?: Handle | string | ElementSpec;
    /** Returns the description of this balloon popup. */
    description: string;
    /** Returns the geometry object that defines the location of this billboard. */
    geometry?: Handle | string | GeometrySpec;
    /** Returns the internal id of this vector element. */
    id?: number;
    /** Returns a copy of the vector element meta data map. The changes you make to this map are NOT reflected in the actual meta data of the element. */
    metaData?: Record<string, Json>;
    position?: Position;
    /** Returns the rotation angle of this billboard. */
    rotation?: number;
    /** Returns the style of this balloon popup. */
    style: Handle | string | ElementstyleSpec;
    /** Returns the title of this balloon popup. */
    title: string;
    /** Returns the state of the visibility flag of this vector element. */
    visible?: boolean;
}

export interface ElementSpec_line {
    type: 'line';
    geometry?: Handle | string | GeometrySpec;
    /** Returns the internal id of this vector element. */
    id?: number;
    /** Returns a copy of the vector element meta data map. The changes you make to this map are NOT reflected in the actual meta data of the element. */
    metaData?: Record<string, Json>;
    poses?: Json;
    /** Returns the style of this line. */
    style: Handle | string | ElementstyleSpec;
    /** Returns the state of the visibility flag of this vector element. */
    visible?: boolean;
}

export interface ElementSpec_marker {
    type: 'marker';
    /** Returns the base billboard this billboard is attached to. */
    baseBillboard?: Handle | string | ElementSpec;
    /** Returns the geometry object that defines the location of this billboard. */
    geometry?: Handle | string | GeometrySpec;
    /** Returns the internal id of this vector element. */
    id?: number;
    /** Returns a copy of the vector element meta data map. The changes you make to this map are NOT reflected in the actual meta data of the element. */
    metaData?: Record<string, Json>;
    position?: Position;
    /** Returns the rotation angle of this billboard. */
    rotation?: number;
    /** Returns the style of this marker. */
    style: Handle | string | ElementstyleSpec;
    /** Returns the state of the visibility flag of this vector element. */
    visible?: boolean;
}

export interface ElementSpec_point {
    type: 'point';
    geometry?: Handle | string | GeometrySpec;
    /** Returns the internal id of this vector element. */
    id?: number;
    /** Returns a copy of the vector element meta data map. The changes you make to this map are NOT reflected in the actual meta data of the element. */
    metaData?: Record<string, Json>;
    position?: Position;
    /** Returns the style of this point. */
    style: Handle | string | ElementstyleSpec;
    /** Returns the state of the visibility flag of this vector element. */
    visible?: boolean;
}

export interface ElementSpec_polygon {
    type: 'polygon';
    geometry?: Handle | string | GeometrySpec;
    holes?: Json;
    /** Returns the internal id of this vector element. */
    id?: number;
    /** Returns a copy of the vector element meta data map. The changes you make to this map are NOT reflected in the actual meta data of the element. */
    metaData?: Record<string, Json>;
    poses?: Json;
    /** Returns the style of this polygon. */
    style: Handle | string | ElementstyleSpec;
    /** Returns the state of the visibility flag of this vector element. */
    visible?: boolean;
}

export interface ElementSpec_text {
    type: 'text';
    /** Returns the base billboard this billboard is attached to. */
    baseBillboard?: Handle | string | ElementSpec;
    /** Returns the geometry object that defines the location of this billboard. */
    geometry?: Handle | string | GeometrySpec;
    /** Returns the internal id of this vector element. */
    id?: number;
    /** Returns a copy of the vector element meta data map. The changes you make to this map are NOT reflected in the actual meta data of the element. */
    metaData?: Record<string, Json>;
    position?: Position;
    /** Returns the rotation angle of this billboard. */
    rotation?: number;
    /** Returns the style of this text label. */
    style: Handle | string | ElementstyleSpec;
    text: string;
    /** Returns the display text. */
    title?: string;
    /** Returns the state of the visibility flag of this vector element. */
    visible?: boolean;
}

export type ElementSpec = ElementSpec_balloon | ElementSpec_line | ElementSpec_marker | ElementSpec_point | ElementSpec_polygon | ElementSpec_text;

export interface ElementstyleSpec_balloon {
    type: 'balloon';
    /** Returns the animation style of the billboard. */
    animationStyle?: Handle;
    /** Returns the horizontal attaching anchor point of the billboard. */
    attachAnchorPointX?: number;
    /** Returns the vertical attaching anchor point of the billboard. */
    attachAnchorPointY?: number;
    /** Returns the margins for popup buttons. */
    buttonMargins?: Json;
    /** Returns the state of the causes overlap flag. */
    causesOverlap?: boolean;
    /** Returns the color of the vector element. */
    color?: number;
    /** Returns the corner radius of the popup. */
    cornerRadius?: number;
    /** Returns the color of the description. */
    descriptionColor?: number;
    /** Returns the description field variable. If not empty, this variable is used to read actual text string from object meta info. */
    descriptionField?: string;
    /** Returns the name of the description font. */
    descriptionFontName?: string;
    /** Returns the size of the description font. */
    descriptionFontSize?: number;
    /** Returns the margins of the description. */
    descriptionMargins?: Json;
    /** Returns the state of the description wrap parameter. */
    descriptionWrap?: boolean;
    /** Returns the state of the allow overlap flag. */
    hideIfOverlapped?: boolean;
    /** Returns the horizontal offset of the billboard. */
    horizontalOffset?: number;
    /** Returns the background color of the left part of the popup. */
    leftColor?: number;
    /** Returns the image of the left part of the popup. */
    leftImage?: Handle | string | Record<string, Json>;
    /** Returns the margins of the left part of the popup. */
    leftMargins?: Json;
    /** Returns the placement priority of the billboard. */
    placementPriority?: number;
    /** Returns the background color of the right part of the popup. */
    rightColor?: number;
    /** Returns the image of the right part of the popup. */
    rightImage?: Handle | string | Record<string, Json>;
    /** Returns the margins of the right part of the popup. */
    rightMargins?: Json;
    /** Returns the state of the scale with DPI flag. */
    scaleWithDPI?: boolean;
    /** Returns the color of the stroke surrounding the popup. */
    strokeColor?: number;
    /** Returns the width of the stroke surrounding the popup. */
    strokeWidth?: number;
    /** Returns the color of the title. */
    titleColor?: number;
    /** Returns the title field variable. If not empty, this variable is used to read actual text string from object meta info. */
    titleField?: string;
    /** Returns the name of the title font. */
    titleFontName?: string;
    /** Returns the size of the title font. */
    titleFontSize?: number;
    /** Returns the margins of the title. */
    titleMargins?: Json;
    /** Returns the state of the title wrap parameter. */
    titleWrap?: boolean;
    /** Returns the height of the triangle at the bottom of the popup. */
    triangleHeight?: number;
    /** Returns the width of the triangle at the bottom of the popup. */
    triangleWidth?: number;
    /** Returns the vertical offset of the billboard. */
    verticalOffset?: number;
}

export interface ElementstyleSpec_line {
    type: 'line';
    /** Returns the bitmap of the line. */
    bitmap?: Handle | string | Record<string, Json>;
    /** Returns the width of the line used for click detection. */
    clickWidth?: number;
    /** Returns the color of the vector element. */
    color?: number;
    /** Returns the end point type of the line. */
    lineEndType?: 'LINE_END_TYPE_NONE' | 'LINE_END_TYPE_SQUARE' | 'LINE_END_TYPE_ROUND' | number;
    /** Returns the join type of the line. */
    lineJoinType?: 'LINE_JOIN_TYPE_NONE' | 'LINE_JOIN_TYPE_MITER' | 'LINE_JOIN_TYPE_BEVEL' | 'LINE_JOIN_TYPE_ROUND' | number;
    /** Returns the stretch factor of the line. */
    stretchFactor?: number;
    /** Returns the width of the line. */
    width?: number;
}

export interface ElementstyleSpec_marker {
    type: 'marker';
    /** Returns the horizontal anchor point of the marker. */
    anchorPointX?: number;
    /** Returns the vertical anchor point of the marker. */
    anchorPointY?: number;
    /** Returns the animation style of the billboard. */
    animationStyle?: Handle;
    /** Returns the horizontal attaching anchor point of the billboard. */
    attachAnchorPointX?: number;
    /** Returns the vertical attaching anchor point of the billboard. */
    attachAnchorPointY?: number;
    /** Returns the bitmap of the marker. */
    bitmap?: Handle | string | Record<string, Json>;
    /** Returns the state of the causes overlap flag. */
    causesOverlap?: boolean;
    /** Returns the size of the marker used for click detection. */
    clickSize?: number;
    /** Returns the color of the vector element. */
    color?: number;
    /** Returns the state of the allow overlap flag. */
    hideIfOverlapped?: boolean;
    /** Returns the horizontal offset of the billboard. */
    horizontalOffset?: number;
    /** Returns the orientation mode of the marker. */
    orientationMode?: 'BILLBOARD_ORIENTATION_FACE_CAMERA' | 'BILLBOARD_ORIENTATION_FACE_CAMERA_GROUND' | 'BILLBOARD_ORIENTATION_GROUND' | number;
    /** Returns the placement priority of the billboard. */
    placementPriority?: number;
    /** Returns the state of the scale with DPI flag. */
    scaleWithDPI?: boolean;
    /** Returns the scaling mode of the marker. */
    scalingMode?: 'BILLBOARD_SCALING_WORLD_SIZE' | 'BILLBOARD_SCALING_SCREEN_SIZE' | 'BILLBOARD_SCALING_CONST_SCREEN_SIZE' | number;
    /** Returns the size of the marker. */
    size?: number;
    /** Returns the vertical offset of the billboard. */
    verticalOffset?: number;
}

export interface ElementstyleSpec_point {
    type: 'point';
    /** Returns the bitmap of the point. */
    bitmap?: Handle | string | Record<string, Json>;
    /** Returns the size of the point used for click detection. */
    clickSize?: number;
    /** Returns the color of the vector element. */
    color?: number;
    /** Returns the size of the point. */
    size?: number;
}

export interface ElementstyleSpec_polygon {
    type: 'polygon';
    /** Returns the color of the vector element. */
    color?: number;
    /** Returns the line style of the edges of the polygon. */
    lineStyle?: Handle | string | ElementstyleSpec;
}

export interface ElementstyleSpec_text {
    type: 'text';
    /** Returns the horizontal anchor point of the label. */
    anchorPointX?: number;
    /** Returns the vertical anchor point of the label. */
    anchorPointY?: number;
    /** Returns the animation style of the billboard. */
    animationStyle?: Handle;
    /** Returns the horizontal attaching anchor point of the billboard. */
    attachAnchorPointX?: number;
    /** Returns the vertical attaching anchor point of the billboard. */
    attachAnchorPointY?: number;
    /** Returns the background color for the text label. */
    backgroundColor?: number;
    /** Returns the border color for the text label. */
    borderColor?: number;
    /** Returns the border width for the text label. */
    borderWidth?: number;
    /** Returns the state of the 'break lines' flag. */
    breakLines?: boolean;
    /** Returns the state of the causes overlap flag. */
    causesOverlap?: boolean;
    /** Returns the color of the vector element. */
    color?: number;
    /** Returns the state of the flippable flag. */
    flippable?: boolean;
    /** Returns the font name for the text label. */
    fontName?: string;
    /** Returns the font size for the text label. */
    fontSize?: number;
    /** Returns the state of the allow overlap flag. */
    hideIfOverlapped?: boolean;
    /** Returns the horizontal offset of the billboard. */
    horizontalOffset?: number;
    /** Returns the orientation mode of the label. */
    orientationMode?: 'BILLBOARD_ORIENTATION_FACE_CAMERA' | 'BILLBOARD_ORIENTATION_FACE_CAMERA_GROUND' | 'BILLBOARD_ORIENTATION_GROUND' | number;
    /** Returns the placement priority of the billboard. */
    placementPriority?: number;
    /** Returns the relative rendering scale for the label. */
    renderScale?: number;
    /** Returns the state of the scale with DPI flag. */
    scaleWithDPI?: boolean;
    /** Returns the scaling mode of the label. */
    scalingMode?: 'BILLBOARD_SCALING_WORLD_SIZE' | 'BILLBOARD_SCALING_SCREEN_SIZE' | 'BILLBOARD_SCALING_CONST_SCREEN_SIZE' | number;
    /** Returns the stroke color for the text label. */
    strokeColor?: number;
    /** Returns the stroke width for the text label. */
    strokeWidth?: number;
    /** Returns the text field variable. If not empty, this variable is used to read actual text string from object meta info. */
    textField?: string;
    /** Returns the margins for the text. */
    textMargins?: Json;
    /** Returns the vertical offset of the billboard. */
    verticalOffset?: number;
}

export type ElementstyleSpec = ElementstyleSpec_balloon | ElementstyleSpec_line | ElementstyleSpec_marker | ElementstyleSpec_point | ElementstyleSpec_polygon | ElementstyleSpec_text;

export interface FeatureSpec_feature {
    type: 'feature';
    geometry: Handle | string | GeometrySpec;
    properties: Json;
}

export type FeatureSpec = FeatureSpec_feature;

export interface GeocodingSpec_multi_osm_offline {
    type: 'multi-osm-offline';
    autocomplete?: boolean;
    language?: string;
    maxResults?: number;
}

export interface GeocodingSpec_multi_osm_offline_reverse {
    type: 'multi-osm-offline-reverse';
    language?: string;
}

export type GeocodingSpec = GeocodingSpec_multi_osm_offline | GeocodingSpec_multi_osm_offline_reverse;

export interface GeometrySpec_line {
    type: 'line';
    poses: Json;
}

export interface GeometrySpec_point {
    type: 'point';
    pos: Position;
}

export interface GeometrySpec_polygon {
    type: 'polygon';
    holes?: Json;
    poses?: Json;
    rings?: Json;
}

/**
 * A geometry read out of GeoJSON, which `buildGeometry` handles itself: every other
 * type has a constructor taking the shape it names, and this one takes a document.
 */
export interface GeometrySpec_geojson {
    type: 'geojson';
    /** The document, as JSON or as the text of it - `GeoJSONGeometryReader` reads either. */
    geojson: Json | string | object;
    /** What to leave the coordinates in. GeoJSON is lon/lat by definition, so this is only for a consumer working in metres. */
    projection?: ProjectionName;
}

export type GeometrySpec = GeometrySpec_line | GeometrySpec_point | GeometrySpec_polygon | GeometrySpec_geojson;

export interface LayerSpec_celestial {
    type: 'celestial';
    /** Returns the object event listener. */
    celestialEventListener?: Handle;
    /** Returns the culling delay of the layer in milliseconds. */
    cullDelay?: number;
    /** Returns a copy of the layer meta data map. The changes you make to this map are NOT reflected in the actual meta data of the layer. */
    metaData?: Record<string, Json>;
    /** Returns the opacity of this layer. */
    opacity?: number;
    /** Returns whether this layer goes through the post-process effect. */
    postProcessed?: boolean;
    /** Returns the layer task priority of this layer. */
    updatePriority?: number;
    /** Returns the visibility of this layer. */
    visible?: boolean;
    /** Returns the visible zoom range of this layer. */
    visibleZoomRange?: [number, number];
}

export interface LayerSpec_composite_vector {
    type: 'composite-vector';
    /** Returns the tile data source of the associated UTF grid. By default this is null. */
    UTFGridDataSource?: Handle | string | SourceSpec;
    /** Returns the UTF grid event listener. */
    UTFGridEventListener?: Handle;
    /** Returns the current display order of the buildings. LAST draws over flat labels too: a label that must clear a building is a billboard one, whose pass runs after the buildings. */
    buildingRenderOrder?: 'VECTOR_TILE_RENDER_ORDER_HIDDEN' | 'VECTOR_TILE_RENDER_ORDER_LAYER' | 'VECTOR_TILE_RENDER_ORDER_LAST' | number;
    /** Returns the click handler layer filter. The filter is given as ECMA regular expression that is applied to qualified layer names. */
    clickHandlerLayerFilter?: string;
    /** Returns the click radius of vector tile features. Units are screen density independent pixels (DP or DIP). */
    clickRadius?: number;
    /** Returns the culling delay of the layer in milliseconds. */
    cullDelay?: number;
    /** Returns the current frame number. */
    frameNr?: number;
    /** Returns the label blending speed, in full fades per second. */
    labelBlendingSpeed?: number;
    /** Returns how much of the perspective divide a label keeps as it recedes from the camera. */
    labelPerspectiveScaling?: number;
    /** Returns the current display order of the labels. */
    labelRenderOrder?: 'VECTOR_TILE_RENDER_ORDER_HIDDEN' | 'VECTOR_TILE_RENDER_ORDER_LAYER' | 'VECTOR_TILE_RENDER_ORDER_LAST' | number;
    /** Returns the current relative layer blending speed. */
    layerBlendingSpeed?: number;
    /** Gets the current maximum overzoom level for this layer. */
    maxOverzoomLevel?: number;
    /** Gets the current maximum underzoom level for this layer. */
    maxUnderzoomLevel?: number;
    /** Returns a copy of the layer meta data map. The changes you make to this map are NOT reflected in the actual meta data of the layer. */
    metaData?: Record<string, Json>;
    /** Returns the opacity of this layer. */
    opacity?: number;
    /** Returns whether this layer goes through the post-process effect. */
    postProcessed?: boolean;
    /** Returns the state of the preloading flag of this layer. */
    preloading?: boolean;
    /** Returns the renderer layer filter. The filter is given as ECMA regular expression that is applied to qualified layer names. */
    rendererLayerFilter?: string;
    source: Handle | string | SourceSpec;
    style: Handle | string | StyleSpec;
    /** Returns the state of the synchronized refresh flag. */
    synchronizedRefresh?: boolean;
    /** Returns the tile cache capacity. */
    tileCacheCapacity?: number;
    /** Returns the tile load listener. */
    tileLoadListener?: Handle;
    /** Returns the current tile substitution policy. */
    tileSubstitutionPolicy?: 'TILE_SUBSTITUTION_POLICY_ALL' | 'TILE_SUBSTITUTION_POLICY_VISIBLE' | 'TILE_SUBSTITUTION_POLICY_NONE' | number;
    /** Returns the layer task priority of this layer. */
    updatePriority?: number;
    /** Returns the vector tile event listener. */
    vectorTileEventListener?: Handle;
    /** Returns the visibility of this layer. */
    visible?: boolean;
    /** Returns the visible zoom range of this layer. */
    visibleZoomRange?: [number, number];
    /** Gets the current zoom level bias for this layer. */
    zoomLevelBias?: number;
}

export interface LayerSpec_elements {
    type: 'elements';
    /** Returns true if Z-buffering is enabled for 2D geometry. By default it is disabled and used only for billboards. */
    ZBuffering?: boolean;
    /** Returns the culling delay of the layer in milliseconds. */
    cullDelay?: number;
    /** Returns a copy of the layer meta data map. The changes you make to this map are NOT reflected in the actual meta data of the layer. */
    metaData?: Record<string, Json>;
    /** Returns the opacity of this layer. */
    opacity?: number;
    /** Returns whether this layer goes through the post-process effect. */
    postProcessed?: boolean;
    source: Handle | string | SourceSpec;
    /** Returns the layer task priority of this layer. */
    updatePriority?: number;
    /** Returns the vector element event listener. */
    vectorElementEventListener?: Handle;
    /** Returns the visibility of this layer. */
    visible?: boolean;
    /** Returns the visible zoom range of this layer. */
    visibleZoomRange?: [number, number];
}

export interface LayerSpec_hillshade {
    type: 'hillshade';
    /** Returns the tile data source of the associated UTF grid. By default this is null. */
    UTFGridDataSource?: Handle | string | SourceSpec;
    /** Returns the UTF grid event listener. */
    UTFGridEventListener?: Handle;
    /** Returns the shading color used to accentuate rugged terrain like sharp cliffs and gorges. */
    accentColor?: number;
    /** Returns the contour line color. */
    contourColor?: number;
    /** Returns whether GPU contour lines are drawn over the hillshade. */
    contourEnabled?: boolean;
    /** Returns the spacing between contour lines in meters. */
    contourInterval?: number;
    /** Returns the contour line half-width in screen pixels. */
    contourWidth?: number;
    /** Returns the contrast of the hillshade overlay. This is the equivalent of MapLibre's 'hillshade-exaggeration' paint property: it controls the slope response curve and the overall strength of the shading, not the relief itself. */
    contrast?: number;
    /** Returns the culling delay of the layer in milliseconds. */
    cullDelay?: number;
    /** Returns whether the normal map encodes absolute elevation (so a custom normal-map lighting shader can call getElevation()). */
    elevationEncodingEnabled?: boolean;
    /** Returns the normal vector tile should be exagerated based on the zoom level. */
    exagerateHeightScaleEnabled?: boolean;
    /** Returns the per-frame relief exaggeration factor, i.e. the vertical exaggeration of the slope. Unlike height scale this is a shader uniform applied at render time (no tile re-decode), so it can be animated smoothly. */
    exaggeration?: number;
    /** Returns the current frame number. */
    frameNr?: number;
    /** Returns the height scale of the hillshade overlay. */
    heightScale?: number;
    /** Returns the shading color of areas that faces towards the light source. */
    highlightColor?: number;
    /** Returns the hillshade rendering method. */
    hillshadeMethod?: 'STANDARD' | 'COMBINED' | 'IGOR' | 'MULTIDIRECTIONAL' | 'BASIC' | number;
    /** Returns the illumination direction of the layer. */
    illuminationDirection?: Position;
    /** Returns whether the illumination direction should change with the map rotation. */
    illuminationMapRotationEnabled?: boolean;
    /** Returns whether the legacy (pre-MapLibre-parity) height scale formula is used. */
    legacyHeightScaleEnabled?: boolean;
    /** Gets the current maximum overzoom level for this layer. */
    maxOverzoomLevel?: number;
    /** Gets the current maximum underzoom level for this layer. */
    maxUnderzoomLevel?: number;
    /** Returns a copy of the layer meta data map. The changes you make to this map are NOT reflected in the actual meta data of the layer. */
    metaData?: Record<string, Json>;
    normalMapLightingShader?: string;
    /** Returns the opacity of this layer. */
    opacity?: number;
    /** Returns whether this layer goes through the post-process effect. */
    postProcessed?: boolean;
    /** Returns the state of the preloading flag of this layer. */
    preloading?: boolean;
    /** Returns the raster tile event listener. */
    rasterTileEventListener?: Handle;
    /** Returns the custom fragment shader source. */
    shaderSource?: string;
    /** Returns the shading color of areas that face away from the light source. */
    shadowColor?: number;
    source: Handle | string | SourceSpec;
    /** Returns the state of the synchronized refresh flag. */
    synchronizedRefresh?: boolean;
    /** Returns whether the layer may shade the 3D terrain's own elevation texture instead of loading a DEM tile set of its own. */
    terrainPaintEnabled?: boolean;
    /** Returns whether the terrain paint shades from the elevation source's own maximum zoom. */
    terrainPaintFullDetailEnabled?: boolean;
    /** Returns the tile texture cache capacity. */
    textureCacheCapacity?: number;
    /** Returns the current relative tile blending speed. */
    tileBlendingSpeed?: number;
    /** Returns the current tile filter mode. */
    tileFilterMode?: 'RASTER_TILE_FILTER_MODE_NEAREST' | 'RASTER_TILE_FILTER_MODE_BILINEAR' | 'RASTER_TILE_FILTER_MODE_BICUBIC' | number;
    /** Returns the tile load listener. */
    tileLoadListener?: Handle;
    /** Returns the current tile substitution policy. */
    tileSubstitutionPolicy?: 'TILE_SUBSTITUTION_POLICY_ALL' | 'TILE_SUBSTITUTION_POLICY_VISIBLE' | 'TILE_SUBSTITUTION_POLICY_NONE' | number;
    /** Returns the layer task priority of this layer. */
    updatePriority?: number;
    /** Returns the visibility of this layer. */
    visible?: boolean;
    /** Returns the visible zoom range of this layer. */
    visibleZoomRange?: [number, number];
    /** Gets the current zoom level bias for this layer. */
    zoomLevelBias?: number;
}

export interface LayerSpec_raster {
    type: 'raster';
    /** Returns the tile data source of the associated UTF grid. By default this is null. */
    UTFGridDataSource?: Handle | string | SourceSpec;
    /** Returns the UTF grid event listener. */
    UTFGridEventListener?: Handle;
    /** Returns the culling delay of the layer in milliseconds. */
    cullDelay?: number;
    /** Returns the current frame number. */
    frameNr?: number;
    /** Gets the current maximum overzoom level for this layer. */
    maxOverzoomLevel?: number;
    /** Gets the current maximum underzoom level for this layer. */
    maxUnderzoomLevel?: number;
    /** Returns a copy of the layer meta data map. The changes you make to this map are NOT reflected in the actual meta data of the layer. */
    metaData?: Record<string, Json>;
    /** Returns the opacity of this layer. */
    opacity?: number;
    /** Returns whether this layer goes through the post-process effect. */
    postProcessed?: boolean;
    /** Returns the state of the preloading flag of this layer. */
    preloading?: boolean;
    /** Returns the raster tile event listener. */
    rasterTileEventListener?: Handle;
    source: Handle | string | SourceSpec;
    /** Returns the state of the synchronized refresh flag. */
    synchronizedRefresh?: boolean;
    /** Returns the tile texture cache capacity. */
    textureCacheCapacity?: number;
    /** Returns the current relative tile blending speed. */
    tileBlendingSpeed?: number;
    /** Returns the current tile filter mode. */
    tileFilterMode?: 'RASTER_TILE_FILTER_MODE_NEAREST' | 'RASTER_TILE_FILTER_MODE_BILINEAR' | 'RASTER_TILE_FILTER_MODE_BICUBIC' | number;
    /** Returns the tile load listener. */
    tileLoadListener?: Handle;
    /** Returns the current tile substitution policy. */
    tileSubstitutionPolicy?: 'TILE_SUBSTITUTION_POLICY_ALL' | 'TILE_SUBSTITUTION_POLICY_VISIBLE' | 'TILE_SUBSTITUTION_POLICY_NONE' | number;
    /** Returns the layer task priority of this layer. */
    updatePriority?: number;
    /** Returns the visibility of this layer. */
    visible?: boolean;
    /** Returns the visible zoom range of this layer. */
    visibleZoomRange?: [number, number];
    /** Gets the current zoom level bias for this layer. */
    zoomLevelBias?: number;
}

export interface LayerSpec_solid {
    type: 'solid';
    /** Returns the bitmap of this layer. */
    bitmap?: Handle | string | Record<string, Json>;
    /** Returns the bitmap scaling factor. */
    bitmapScale?: number;
    /** Returns the color of this layer. */
    color?: number | Json;
    /** Returns the culling delay of the layer in milliseconds. */
    cullDelay?: number;
    /** Returns a copy of the layer meta data map. The changes you make to this map are NOT reflected in the actual meta data of the layer. */
    metaData?: Record<string, Json>;
    /** Returns the opacity of this layer. */
    opacity?: number;
    /** Returns whether this layer goes through the post-process effect. */
    postProcessed?: boolean;
    /** Returns the layer task priority of this layer. */
    updatePriority?: number;
    /** Returns the visibility of this layer. */
    visible?: boolean;
    /** Returns the visible zoom range of this layer. */
    visibleZoomRange?: [number, number];
}

export interface LayerSpec_vector {
    type: 'vector';
    /** Returns the tile data source of the associated UTF grid. By default this is null. */
    UTFGridDataSource?: Handle | string | SourceSpec;
    /** Returns the UTF grid event listener. */
    UTFGridEventListener?: Handle;
    /** Returns the current display order of the buildings. LAST draws over flat labels too: a label that must clear a building is a billboard one, whose pass runs after the buildings. */
    buildingRenderOrder?: 'VECTOR_TILE_RENDER_ORDER_HIDDEN' | 'VECTOR_TILE_RENDER_ORDER_LAYER' | 'VECTOR_TILE_RENDER_ORDER_LAST' | number;
    /** Returns the click handler layer filter. The filter is given as ECMA regular expression that is applied to qualified layer names. */
    clickHandlerLayerFilter?: string;
    /** Returns the click radius of vector tile features. Units are screen density independent pixels (DP or DIP). */
    clickRadius?: number;
    /** Returns the culling delay of the layer in milliseconds. */
    cullDelay?: number;
    /** Returns the current frame number. */
    frameNr?: number;
    /** Returns the label blending speed, in full fades per second. */
    labelBlendingSpeed?: number;
    /** Returns how much of the perspective divide a label keeps as it recedes from the camera. */
    labelPerspectiveScaling?: number;
    /** Returns the current display order of the labels. */
    labelRenderOrder?: 'VECTOR_TILE_RENDER_ORDER_HIDDEN' | 'VECTOR_TILE_RENDER_ORDER_LAYER' | 'VECTOR_TILE_RENDER_ORDER_LAST' | number;
    /** Returns the current relative layer blending speed. */
    layerBlendingSpeed?: number;
    /** Gets the current maximum overzoom level for this layer. */
    maxOverzoomLevel?: number;
    /** Gets the current maximum underzoom level for this layer. */
    maxUnderzoomLevel?: number;
    /** Returns a copy of the layer meta data map. The changes you make to this map are NOT reflected in the actual meta data of the layer. */
    metaData?: Record<string, Json>;
    /** Returns the opacity of this layer. */
    opacity?: number;
    /** Returns whether this layer goes through the post-process effect. */
    postProcessed?: boolean;
    /** Returns the state of the preloading flag of this layer. */
    preloading?: boolean;
    /** Returns the renderer layer filter. The filter is given as ECMA regular expression that is applied to qualified layer names. */
    rendererLayerFilter?: string;
    source: Handle | string | SourceSpec;
    style: Handle | string | StyleSpec;
    /** Returns the state of the synchronized refresh flag. */
    synchronizedRefresh?: boolean;
    /** Returns the tile cache capacity. */
    tileCacheCapacity?: number;
    /** Returns the tile load listener. */
    tileLoadListener?: Handle;
    /** Returns the current tile substitution policy. */
    tileSubstitutionPolicy?: 'TILE_SUBSTITUTION_POLICY_ALL' | 'TILE_SUBSTITUTION_POLICY_VISIBLE' | 'TILE_SUBSTITUTION_POLICY_NONE' | number;
    /** Returns the layer task priority of this layer. */
    updatePriority?: number;
    /** Returns the vector tile event listener. */
    vectorTileEventListener?: Handle;
    /** Returns the visibility of this layer. */
    visible?: boolean;
    /** Returns the visible zoom range of this layer. */
    visibleZoomRange?: [number, number];
    /** Gets the current zoom level bias for this layer. */
    zoomLevelBias?: number;
}

export type LayerSpec = LayerSpec_celestial | LayerSpec_composite_vector | LayerSpec_elements | LayerSpec_hillshade | LayerSpec_raster | LayerSpec_solid | LayerSpec_vector;

export interface OptionsSpec_fog {
    type: 'fog';
    /** Returns the fog color. */
    color?: number;
    /** Returns whether the fog is drawn at all. */
    enabled?: boolean;
    /** Returns the color of the upper atmosphere. */
    highColor?: number;
    /** Returns how far up the sky the fog is blended in. */
    horizonBlend?: number;
    /** Returns where the fog reaches full strength. */
    rangeEnd?: number;
    /** Returns where the fog starts. */
    rangeStart?: number;
    /** Returns the custom fog fragment shader source, or an empty string if the built-in blend is used. */
    shaderSource?: string;
    /** Returns the color of the sky at the zenith, beyond the atmosphere. */
    spaceColor?: number;
    /** Returns how brightly stars are drawn beyond the atmosphere. */
    starIntensity?: number;
    /** Returns the altitude the fog has fully faded out at. */
    verticalRangeEnd?: number;
    /** Returns the altitude the fog starts fading out at. */
    verticalRangeStart?: number;
}

export interface OptionsSpec_light {
    type: 'light';
    /** Returns the ambient light color. */
    ambientColor?: number;
    /** Returns the ambient light intensity. */
    ambientIntensity?: number;
    /** Returns the day-cycle light curve. */
    dayCycleLightStops?: Json;
    /** Returns whether the sun's COLOURS follow its position. */
    dayCycleLightsEnabled?: boolean;
    /** Returns the curve used while the sun is RISING, if the app set one. */
    dayCycleRisingLightStops?: Json;
    /** Returns the shadow depth bias scale. */
    shadowBias?: number;
    /** Returns the number of shadow cascades. */
    shadowCascades?: number;
    /** Returns the shadow caster margin in tiles. */
    shadowCasterMargin?: number;
    /** Returns the shadow distance. */
    shadowDistance?: number;
    /** Returns the shadow map resolution. */
    shadowMapSize?: number;
    /** Returns the shadow normal offset. */
    shadowNormalOffset?: number;
    /** Returns the shadow softness. */
    shadowSoftness?: number;
    /** Returns the shadow strength. */
    shadowStrength?: number;
    /** Returns the sun altitude in degrees above the horizon. */
    sunAltitude?: number;
    /** Returns the sun azimuth in degrees. */
    sunAzimuth?: number;
    /** Returns the sun (directional light) color. */
    sunColor?: number;
    /** Returns the sun light intensity. */
    sunIntensity?: number;
    /** Returns whether this sun overrides the one a style states. */
    sunOverridingStyle?: boolean;
    /** Returns whether the sun lights the 3D terrain surface. */
    terrainLightingEnabled?: boolean;
}

export interface OptionsSpec_sky {
    type: 'sky';
    /** Returns the tint applied to Rayleigh scattering. */
    atmosphereColor?: number;
    /** Returns the exposure applied to the scattered light. */
    atmosphereLuminance?: number;
    /** Returns the brightness of the sun driving the atmosphere. */
    atmosphereSunIntensity?: number;
    /** Returns whether the shader sky is enabled. */
    enabled?: boolean;
    /** Returns the ground color. */
    groundColor?: number;
    /** Returns the tint applied to Mie scattering. */
    haloColor?: number;
    /** Returns the angular blend width between the horizon color and the sky color. */
    horizonBlend?: number;
    /** Returns the horizon color. */
    horizonColor?: number;
    /** Returns how finely the atmosphere is integrated. */
    quality?: 'SKY_QUALITY_LOW' | 'SKY_QUALITY_MEDIUM' | 'SKY_QUALITY_HIGH' | number;
    /** Returns the custom sky fragment shader source, or an empty string if the built-in shader is used. */
    shaderSource?: string;
    /** Returns the zenith sky color. */
    skyColor?: number;
    /** Returns whether the built-in shader draws a sun disc. */
    sunDiscEnabled?: boolean;
    /** Returns what the sky pass draws. */
    type?: 'SKY_TYPE_GRADIENT' | 'SKY_TYPE_ATMOSPHERE' | number;
}

export interface OptionsSpec_terrain {
    type: 'terrain';
    /** Returns how long the terrain takes to sink flat. */
    autoFlattenDuration?: number;
    /** Returns the screen parallax below which the terrain renders flat. */
    autoFlattenParallax?: number;
    /** Returns how long the terrain takes to rise back into 3D. */
    autoFlattenRiseDuration?: number;
    /** Returns the tilt at or above which the terrain renders flat. */
    autoFlattenTilt?: number;
    /** Returns the terrain background color. */
    backgroundColor?: number;
    /** Returns the billboard/label terrain occlusion state. */
    billboardOcclusionEnabled?: boolean;
    /** Returns the billboard/label terrain occlusion tolerance. */
    billboardOcclusionTolerance?: number;
    /** Returns whether bridges and tunnels stand on their own chord (3D bridges). */
    bridges3DEnabled?: boolean;
    /** Returns the duration of the camera terrain-following correction animation. */
    cameraClampDuration?: number;
    /** Returns the camera terrain clearance floor: an explicit minimum height the camera is kept above the terrain surface, in meters. */
    cameraClearance?: number;
    /** Returns the share of the camera's altitude that the terrain clearance takes. */
    cameraClearanceFraction?: number;
    /** Returns the clip-space depth bias used when depth-testing draped 2D geometry against the terrain. */
    depthBias?: number;
    /** Returns the drape cache budget in megabytes. */
    drapeCacheSize?: number;
    /** Returns whether polygon fills are draped as a render-to-texture surface. */
    drapeFillsEnabled?: boolean;
    /** Returns whether vt tile lines are also draped (in addition to fills). */
    drapeLinesEnabled?: boolean;
    /** Returns the per-tile drape texture resolution, 0 when it follows the screen. */
    drapeResolution?: number;
    /** Returns how many drape tiles the automatic resolution assumes are cached at once. */
    drapeWorkingSet?: number;
    /** Returns the elevation grid cache budget in megabytes, 0 for the SDK's own rule. */
    elevationCacheSize?: number;
    /** Returns whether elevation tile prefetching is enabled. */
    elevationPrefetchEnabled?: boolean;
    /** Returns the enabled state of the terrain. */
    enabled?: boolean;
    /** Returns the terrain height exaggeration factor. */
    exaggeration?: number;
    /** Returns how far a flattened terrain goes back towards a plain 2D map. */
    flattenMode?: 'TERRAIN_FLATTEN_MODE_RENDER' | 'TERRAIN_FLATTEN_MODE_FULL' | number;
    /** Returns how far the terrain is flattened right now, 0 (full 3D) to 1 (flat). */
    flattenRatio?: number;
    /** Returns whether the map is asked to render flat. This is the 2D/3D state, whether it was set by the app or by auto-flattening; the switch itself is animated, so for a moment after a change the map is still on its way there. */
    flattened?: boolean;
    /** Returns the height the viewpoint is lifted above the ground-following focus, in meters. */
    focusLift?: number;
    /** Returns how many zoom levels below the camera a tile may coarsen to. */
    maxTileZoomCoarsening?: number;
    /** Returns the maximum visible tile zoom offset, relative to the camera zoom level. */
    maxTileZoomOffset?: number;
    /** Returns the maximum tile zoom level the terrain mesh is cut at. */
    maxZoom?: number;
    /** Returns how many terrain surface meshes may be cached. */
    meshCacheSize?: number;
    /** Returns the terrain mesh resolution. */
    meshResolution?: number;
    /** Returns the minimum tile zoom level with 3D terrain. */
    minZoom?: number;
    /** Returns the style layers that are kept out of the terrain drape bake. */
    noDrapeLayerFilter?: string;
    /** Returns the ground distance the surface normals are measured over, in meters. */
    normalSampleDistance?: number;
    /** Returns the downscale factor of the packed depth/normal texture post-process effects read. */
    postProcessDownscale?: number;
    /** Returns whether seamless tile edge handling is enabled. */
    seamlessTileEdgesEnabled?: boolean;
    /** Returns whether the shared ground pass draws the terrain a second time. */
    sharedGroundEnabled?: boolean;
    source: Handle | string | SourceSpec;
    /** Returns the distance geo-three's terrain LOD subdivides at. */
    subdivideDistance?: number;
    /** Returns the resolution the elevation node field is built at. */
    surfaceNodeResolution?: number;
    /** Returns the custom terrain surface fragment shader source, or an empty string if no shaded surface is drawn. */
    surfaceShaderSource?: string;
    /** Returns the opacity a label keeps while its anchor is behind 3D content. */
    textOcclusionOpacity?: number;
    /** Returns whether cross-LOD tile edge stitching is enabled. */
    tileEdgeStitchingEnabled?: boolean;
    /** Returns the minimum view distance, in meters. */
    viewDistance?: number;
    /** Returns the factor applied to the view distance. */
    viewDistanceFactor?: number;
    /** Returns the maximum view distance, in meters. */
    viewDistanceMax?: number;
}

export type OptionsSpec = OptionsSpec_fog | OptionsSpec_light | OptionsSpec_sky | OptionsSpec_terrain;

export interface RoutingSpec_multi_valhalla_offline {
    type: 'multi-valhalla-offline';
    profile?: string;
}

export interface RoutingSpec_valhalla_offline {
    type: 'valhalla-offline';
    path: string;
    profile?: string;
}

export interface RoutingSpec_valhalla_online {
    type: 'valhalla-online';
    /** Returns the current set of HTTP headers used. Initially this set is empty and can be changed with setHTTPHeaders. */
    HTTPHeaders?: Record<string, string>;
    apiKey?: string;
    /** Returns the custom backend service URL. */
    customServiceURL?: string;
    profile?: string;
    /** Returns the current timeout value. */
    timeout?: number;
}

export type RoutingSpec = RoutingSpec_multi_valhalla_offline | RoutingSpec_valhalla_offline | RoutingSpec_valhalla_online;

export interface SearchSpec_request {
    type: 'request';
    /** Returns the string based search expression. If empty, then search expression is not used. */
    filterExpression?: string;
    /** Returns the geometry used for proximity search. */
    geometry?: Handle | string | GeometrySpec;
    /** Returns the projection to use for search geometry. */
    projection?: Handle | string | Record<string, Json>;
    /** Returns the regular expression used to search all the fields. If empty, then the regular expression is not used. */
    regexFilter?: string;
    /** Returns the search radius for proximity search (in meters). The default is 0. */
    searchRadius?: number;
}

export interface SearchSpec_vectortile {
    type: 'vectortile';
    /** Returns the layers to filter while decoding tiles. */
    layers?: string[];
    /** Returns the maximum number of results the search service returns. */
    maxResults?: number;
    /** Returns the maximum zoom level of vector tiles used. By default the maximum zoom level is specified by data source. */
    maxZoom?: number;
    /** Returns the minimum zoom level of vector tiles used. By default the minimum zoom level is specified by data source and is usually 0. */
    minZoom?: number;
    /** Returns wether to prevent duplicate elements */
    preventDuplicates?: boolean;
    /** Returns wether result features are sorted by distance */
    sortByDistance?: boolean;
    source: Handle | string | SourceSpec;
    style: Handle | string | StyleSpec;
}

/**
 * A vector tile search over a layer that is already on the map.
 *
 * The constructor takes a source and a decoder; this takes the LAYER and reads both off
 * it, so a search reads exactly what the user is looking at and neither is built twice.
 */
export interface SearchSpec_vectortile_layer {
    type: 'vectortile';
    /** The vector tile layer to search - its data source and its tile decoder are taken from it. */
    layer: Handle | string | LayerSpec;
    /** Returns the layers to filter while decoding tiles. */
    layers?: string[];
    /** Returns the maximum number of results the search service returns. */
    maxResults?: number;
    /** Returns the maximum zoom level of vector tiles used. By default the maximum zoom level is specified by data source. */
    maxZoom?: number;
    /** Returns the minimum zoom level of vector tiles used. By default the minimum zoom level is specified by data source and is usually 0. */
    minZoom?: number;
    /** Returns wether to prevent duplicate elements */
    preventDuplicates?: boolean;
    /** Returns wether result features are sorted by distance */
    sortByDistance?: boolean;
}

export type SearchSpec = SearchSpec_request | SearchSpec_vectortile | SearchSpec_vectortile_layer;

export interface SourceSpec_assets {
    type: 'assets';
    /** Gets the current maximum overzoom level for this datasource. Over it the datasource will not be "drawn" */
    maxOverzoomLevel?: number;
    maxZoom?: number;
    /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
    metaData?: Record<string, Json>;
    minZoom?: number;
    path: string;
}

export interface SourceSpec_combined {
    type: 'combined';
    /** Gets the current maximum overzoom level for this datasource. Over it the datasource will not be "drawn" */
    maxOverzoomLevel?: number;
    /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
    metaData?: Record<string, Json>;
    source: Handle | string | SourceSpec;
    source2: Handle | string | SourceSpec;
    zoomLevel?: number;
}

export interface SourceSpec_contour {
    type: 'contour';
    /** Returns the base contour interval in meters. */
    baseInterval?: number;
    /** Returns the contour interval used for label stubs. */
    labelInterval?: number;
    /** Returns whether only short label stubs are generated instead of full contour lines. */
    labelStubsEnabled?: boolean;
    /** Returns the name of the generated vector tile layer. */
    layerName?: string;
    /** Gets the current maximum overzoom level for this datasource. Over it the datasource will not be "drawn" */
    maxOverzoomLevel?: number;
    /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
    metaData?: Record<string, Json>;
    /** Returns the minimum zoom at which contour geometry is generated. */
    minVisibleZoom?: number;
    /** Returns the target grid resolution used for contour tracing. */
    resolution?: number;
    /** Returns whether seamless tile edges are enabled. */
    seamlessEdgesEnabled?: boolean;
    /** Returns the simplification tolerance in tile pixels. */
    simplifyTolerance?: number;
    source: Handle | string | SourceSpec;
    /** Returns the terrain options whose elevation manager the label stubs read. */
    terrainOptions?: Handle | string | OptionsSpec;
}

export interface SourceSpec_geojson {
    type: 'geojson';
    /** Returns the default layer buffer in tile pixels. */
    defaultLayerBuffer?: number;
    /** Gets the current maximum overzoom level for this datasource. Over it the datasource will not be "drawn" */
    maxOverzoomLevel?: number;
    maxZoom?: number;
    /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
    metaData?: Record<string, Json>;
    minZoom?: number;
    /** Returns the simplification tolerance in tile pixels. */
    simplifyTolerance?: number;
}

export interface SourceSpec_http {
    type: 'http';
    /** Returns the current set of HTTP headers used. Initially this set is empty and can be changed with setHTTPHeaders. */
    HTTPHeaders?: Record<string, string>;
    /** Returns true/false based whether the TMS tiling scheme is used. */
    TMSScheme?: boolean;
    /** Returns true/false based on whether the max-age header check is used. If this is enabled, SDK will automatically refresh the tiles when tiles have expired. */
    maxAgeHeaderCheck?: boolean;
    /** Gets the current maximum overzoom level for this datasource. Over it the datasource will not be "drawn" */
    maxOverzoomLevel?: number;
    maxZoom?: number;
    /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
    metaData?: Record<string, Json>;
    minZoom?: number;
    /** Returns the subdomains for {s} tag. The default is ["a", "b", "c", "d"]. */
    subdomains?: string[];
    /** Returns the current timeout value. */
    timeout?: number;
    /** Returns the base URL template containing tags. */
    url: string;
}

export interface SourceSpec_local {
    type: 'local';
    /** Returns the active geometry simplifier of the data source. */
    geometrySimplifier?: Handle;
    projection: Handle | string | Record<string, Json>;
    spatialIndexType?: 'LOCAL_SPATIAL_INDEX_TYPE_NULL' | 'LOCAL_SPATIAL_INDEX_TYPE_KDTREE';
}

export interface SourceSpec_maptiler {
    type: 'maptiler';
    /** Returns the custom backend service URL. */
    customServiceURL?: string;
    key: string;
    /** Gets the current maximum overzoom level for this datasource. Over it the datasource will not be "drawn" */
    maxOverzoomLevel?: number;
    /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
    metaData?: Record<string, Json>;
    /** Returns the current timeout value. */
    timeout?: number;
}

export interface SourceSpec_mbtiles {
    type: 'mbtiles';
    /** Gets the current maximum overzoom level for this datasource. Over it the datasource will not be "drawn" */
    maxOverzoomLevel?: number;
    maxZoom?: number;
    /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
    metaData?: Record<string, Json>;
    minZoom?: number;
    path: string;
    scheme?: 'MBTILES_SCHEME_TMS' | 'MBTILES_SCHEME_XYZ';
}

export interface SourceSpec_memory_cache {
    type: 'memory-cache';
    capacity?: number;
    /** Gets the current maximum overzoom level for this datasource. Over it the datasource will not be "drawn" */
    maxOverzoomLevel?: number;
    /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
    metaData?: Record<string, Json>;
    source: Handle | string | SourceSpec;
}

export interface SourceSpec_merged_mbvt {
    type: 'merged-mbvt';
    /** Gets the current maximum overzoom level for this datasource. Over it the datasource will not be "drawn" */
    maxOverzoomLevel?: number;
    /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
    metaData?: Record<string, Json>;
    source: Handle | string | SourceSpec;
    source2: Handle | string | SourceSpec;
}

export interface SourceSpec_multi {
    type: 'multi';
    maxOpenedPackages?: number;
    /** Gets the current maximum overzoom level for this datasource. Over it the datasource will not be "drawn" */
    maxOverzoomLevel?: number;
    /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
    metaData?: Record<string, Json>;
}

export interface SourceSpec_ordered {
    type: 'ordered';
    /** Gets the current maximum overzoom level for this datasource. Over it the datasource will not be "drawn" */
    maxOverzoomLevel?: number;
    /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
    metaData?: Record<string, Json>;
    source: Handle | string | SourceSpec;
    source2: Handle | string | SourceSpec;
}

export interface SourceSpec_persistent_cache {
    type: 'persistent-cache';
    /** Returns the state of cache only mode. */
    cacheOnlyMode?: boolean;
    capacity?: number;
    databasePath: string;
    /** Gets the current maximum overzoom level for this datasource. Over it the datasource will not be "drawn" */
    maxOverzoomLevel?: number;
    /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
    metaData?: Record<string, Json>;
    source: Handle | string | SourceSpec;
}

export interface SourceSpec_pmtiles {
    type: 'pmtiles';
    /** Gets the current maximum overzoom level for this datasource. Over it the datasource will not be "drawn" */
    maxOverzoomLevel?: number;
    maxZoom?: number;
    /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
    metaData?: Record<string, Json>;
    minZoom?: number;
    path: string;
}

export interface SourceSpec_point_detail {
    type: 'point-detail';
    /** Returns the zoom whose tiles are read. */
    detailZoom?: number;
    layer: string;
    /** Returns how many zoom levels below the requested tile this will reach. */
    maxDetailLevels?: number;
    /** Returns how many features a rebuilt tile may carry. */
    maxFeatures?: number;
    /** Gets the current maximum overzoom level for this datasource. Over it the datasource will not be "drawn" */
    maxOverzoomLevel?: number;
    /** Returns a copy of the data source meta data map; changes to it are not reflected in the source. The map is attached to every loaded tile, e.g. "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder. A wrapper source with no map of its own answers with its wrapped source's. */
    metaData?: Record<string, Json>;
    /** Returns the property a rebuilt tile's features are ranked by. */
    rankProperty?: string;
    source: Handle | string | SourceSpec;
}

export type SourceSpec = SourceSpec_assets | SourceSpec_combined | SourceSpec_contour | SourceSpec_geojson | SourceSpec_http | SourceSpec_local | SourceSpec_maptiler | SourceSpec_mbtiles | SourceSpec_memory_cache | SourceSpec_merged_mbvt | SourceSpec_multi | SourceSpec_ordered | SourceSpec_persistent_cache | SourceSpec_pmtiles | SourceSpec_point_detail;

export interface StyleSpec_mbvt {
    type: 'mbvt';
    /** Returns the current CartoCSS style set used by the decoder. If decoder uses non-CartoCSS style set, null is returned. */
    cartoCSSStyle?: Handle | string | StylesetSpec;
    cartocss?: Handle | string | StylesetSpec;
    /** Returns the current compiled style set used by the decoder. If decoder uses non-compiled style set, null is returned. */
    compiledStyle?: Handle | string | StylesetSpec;
    /** Returns the value of feature id override flag. This is intended for cases when feature ids in tile are not globally unique. */
    featureIdOverride?: boolean;
    /** Returns the value of the specified style parameter. The style parameter must be declared in the current style. */
    params?: Record<string, string>;
    project?: Handle | string | StylesetSpec;
    /** Returns the binary format the tiles are decoded as. */
    tileFormat?: 'TILE_FORMAT_AUTO' | 'TILE_FORMAT_MVT' | 'TILE_FORMAT_MLT' | number;
}

export type StyleSpec = StyleSpec_mbvt;

export interface StylesetSpec_cartocss {
    type: 'cartocss';
    assets?: Handle | string | AssetsSpec;
    css: string;
}

export interface StylesetSpec_project {
    type: 'project';
    assets: Handle | string | AssetsSpec;
    name?: string;
}

export type StylesetSpec = StylesetSpec_cartocss | StylesetSpec_project;

export interface SpecOf {
    'assets': AssetsSpec;
    'celestial': CelestialSpec;
    'effect': EffectSpec;
    'element': ElementSpec;
    'elementstyle': ElementstyleSpec;
    'feature': FeatureSpec;
    'geocoding': GeocodingSpec;
    'geometry': GeometrySpec;
    'layer': LayerSpec;
    'options': OptionsSpec;
    'routing': RoutingSpec;
    'search': SearchSpec;
    'source': SourceSpec;
    'style': StyleSpec;
    'styleset': StylesetSpec;
}
export type Kind = keyof SpecOf & string;

/** @internal The class each spec type builds - see ClassOfSpec. */
export interface SpecClass {
    'assets': {
        'bundle': 'massif::BundleAssetPackage';
        'dir': 'massif::DirAssetPackage';
        'zip': 'massif::ZippedAssetPackage';
    };
    'celestial': {
        'arc': 'massif::CelestialArc';
        'label': 'massif::CelestialLabel';
        'sprite': 'massif::CelestialSprite';
    };
    'effect': {
        'postprocess': 'massif::PostProcessEffect';
    };
    'element': {
        'balloon': 'massif::BalloonPopup';
        'line': 'massif::Line';
        'marker': 'massif::Marker';
        'point': 'massif::Point';
        'polygon': 'massif::Polygon';
        'text': 'massif::Text';
    };
    'elementstyle': {
        'balloon': 'massif::BalloonPopupStyle';
        'line': 'massif::LineStyle';
        'marker': 'massif::MarkerStyle';
        'point': 'massif::PointStyle';
        'polygon': 'massif::PolygonStyle';
        'text': 'massif::TextStyle';
    };
    'feature': {
        'feature': 'massif::Feature';
    };
    'geocoding': {
        'multi-osm-offline': 'massif::MultiOSMOfflineGeocodingService';
        'multi-osm-offline-reverse': 'massif::MultiOSMOfflineReverseGeocodingService';
    };
    'geometry': {
        'line': 'massif::LineGeometry';
        'point': 'massif::PointGeometry';
        'polygon': 'massif::PolygonGeometry';
        'geojson': 'massif::Geometry';
    };
    'layer': {
        'celestial': 'massif::CelestialLayer';
        'composite-vector': 'massif::CompositeVectorTileLayer';
        'elements': 'massif::VectorLayer';
        'hillshade': 'massif::HillshadeRasterTileLayer';
        'raster': 'massif::RasterTileLayer';
        'solid': 'massif::SolidLayer';
        'vector': 'massif::VectorTileLayer';
    };
    'options': {
        'fog': 'massif::FogOptions';
        'light': 'massif::LightOptions';
        'sky': 'massif::SkyOptions';
        'terrain': 'massif::TerrainOptions';
    };
    'routing': {
        'multi-valhalla-offline': 'massif::MultiValhallaOfflineRoutingService';
        'valhalla-offline': 'massif::ValhallaOfflineRoutingService';
        'valhalla-online': 'massif::ValhallaOnlineRoutingService';
    };
    'search': {
        'request': 'massif::SearchRequest';
        'vectortile': 'massif::VectorTileSearchService';
    };
    'source': {
        'assets': 'massif::AssetTileDataSource';
        'combined': 'massif::CombinedTileDataSource';
        'contour': 'massif::ContourTileDataSource';
        'geojson': 'massif::GeoJSONVectorTileDataSource';
        'http': 'massif::HTTPTileDataSource';
        'local': 'massif::LocalVectorDataSource';
        'maptiler': 'massif::MapTilerOnlineTileDataSource';
        'mbtiles': 'massif::MBTilesTileDataSource';
        'memory-cache': 'massif::MemoryCacheTileDataSource';
        'merged-mbvt': 'massif::MergedMBVTTileDataSource';
        'multi': 'massif::MultiTileDataSource';
        'ordered': 'massif::OrderedTileDataSource';
        'persistent-cache': 'massif::PersistentCacheTileDataSource';
        'pmtiles': 'massif::PMTilesTileDataSource';
        'point-detail': 'massif::PointDetailTileDataSource';
    };
    'style': {
        'mbvt': 'massif::MBVectorTileDecoder';
    };
    'styleset': {
        'cartocss': 'massif::CartoCSSStyleSet';
        'project': 'massif::CompiledStyleSet';
    };
}

// --- methods -------------------------------------------------------------
//
// Arguments are a labelled tuple, so an editor shows the parameter names.

/** @internal A lookup table - see MethodName, MethodArgs and MethodResult. */
export interface MethodTypes {
    'massif::Address': {
    };
    'massif::AnimationStyle': {
    };
    'massif::AnimationStyleBuilder': {
    };
    'massif::AssetPackage': {
    };
    'massif::AssetTileDataSource': {
        getMetaDataElement: { args: [key: string]; result: Json };
        loadTile: { args: [tile: Tile]; result: Handle<'massif::TileData'>; resultClass: 'massif::TileData' };
        setMetaDataElement: { args: [key: string, value: Json]; result: void };
    };
    'massif::BalloonPopup': {
    };
    'massif::BalloonPopupButton': {
    };
    'massif::BalloonPopupButtonClickInfo': {
    };
    'massif::BalloonPopupButtonStyle': {
    };
    'massif::BalloonPopupButtonStyleBuilder': {
    };
    'massif::BalloonPopupEventListener': {
    };
    'massif::BalloonPopupMargins': {
    };
    'massif::BalloonPopupStyle': {
    };
    'massif::BalloonPopupStyleBuilder': {
    };
    'massif::BaseMapView': {
        fitBounds: { args: [bounds: Json, screenBounds: Json, integerZoom: boolean, resetRotation: boolean, resetTilt: boolean, durationSeconds: number]; result: void };
        flyTo: { args: [pos: Position, zoom: number, rotation: number, tilt: number, climbHeight: number, durationSeconds: number, easing: string]; result: void };
        mapToScreen: { args: [pos: Position]; result: Json };
        moveCameraTo: { args: [pos: Position, zoom: number, rotation: number, tilt: number]; result: void };
        moveTo: { args: [pos: Position, zoom: number, rotation: number, tilt: number]; result: void };
        screenToMap: { args: [x: number, y: number]; result: Json };
        stopFlight: { args: []; result: void };
    };
    'massif::Billboard': {
    };
    'massif::BillboardStyle': {
    };
    'massif::BillboardStyleBuilder': {
    };
    'massif::BinaryData': {
    };
    'massif::Bitmap': {
    };
    'massif::BitmapOverlayRasterTileDataSource': {
        getMetaDataElement: { args: [key: string]; result: Json };
        loadTile: { args: [tile: Tile]; result: Handle<'massif::TileData'>; resultClass: 'massif::TileData' };
        setMetaDataElement: { args: [key: string, value: Json]; result: void };
    };
    'massif::BundleAssetPackage': {
    };
    'massif::CacheTileDataSource': {
        getMetaDataElement: { args: [key: string]; result: Json };
        loadTile: { args: [tile: Tile]; result: Handle<'massif::TileData'>; resultClass: 'massif::TileData' };
        setMetaDataElement: { args: [key: string, value: Json]; result: void };
    };
    'massif::CartoCSSStyleSet': {
    };
    'massif::CelestialArc': {
        setCircle: { args: [axisAzimuth: number, axisAltitude: number, radius: number]; result: void };
        setDirection: { args: [azimuth: number, altitude: number, distance: number]; result: void };
        setDirections: { args: [directions: Json]; result: void };
        setSegments: { args: [directions: Json]; result: void };
    };
    'massif::CelestialClickInfo': {
    };
    'massif::CelestialEventListener': {
    };
    'massif::CelestialLabel': {
        setAnchorPoint: { args: [x: number, y: number]; result: void };
        setDirection: { args: [azimuth: number, altitude: number, distance: number]; result: void };
        setOffset: { args: [x: number, y: number]; result: void };
    };
    'massif::CelestialLayer': {
        add: { args: [object: Handle]; result: void };
        clear: { args: []; result: void };
        refresh: { args: []; result: void };
        remove: { args: [object: Handle]; result: boolean };
    };
    'massif::CelestialObject': {
        setDirection: { args: [azimuth: number, altitude: number, distance: number]; result: void };
    };
    'massif::CelestialSprite': {
        setDirection: { args: [azimuth: number, altitude: number, distance: number]; result: void };
    };
    'massif::ClickInfo': {
    };
    'massif::ClusterElementBuilder': {
    };
    'massif::ClusterFetchTask': {
    };
    'massif::ClusteredVectorLayer': {
        refresh: { args: []; result: void };
    };
    'massif::Color': {
    };
    'massif::CombinedTileDataSource': {
        getMetaDataElement: { args: [key: string]; result: Json };
        loadTile: { args: [tile: Tile]; result: Handle<'massif::TileData'>; resultClass: 'massif::TileData' };
        setMetaDataElement: { args: [key: string, value: Json]; result: void };
    };
    'massif::CompiledStyleSet': {
    };
    'massif::CompositeVectorTileLayer': {
        addExternalDataSource: { args: [name: string, dataSource: Handle, type: number]; result: void };
        addVectorDataSource: { args: [name: string, dataSource: Handle]; result: void };
        clearTileCaches: { args: [all: boolean]; result: void };
        getExternalChildLayer: { args: [name: string]; result: Handle<'massif::Layer'>; resultClass: 'massif::Layer' };
        getExternalDataSourceNames: { args: []; result: Json };
        refresh: { args: []; result: void };
        removeExternalDataSource: { args: [name: string]; result: boolean };
        setExternalDataSourceMaxOverzoomLevel: { args: [name: string, level: number]; result: void };
        setExternalDataSourceZoomLevelBias: { args: [name: string, bias: number]; result: void };
    };
    'massif::ContourTileDataSource': {
        getMetaDataElement: { args: [key: string]; result: Json };
        loadTile: { args: [tile: Tile]; result: Handle<'massif::TileData'>; resultClass: 'massif::TileData' };
        setMetaDataElement: { args: [key: string, value: Json]; result: void };
    };
    'massif::CullState': {
    };
    'massif::CustomPopup': {
    };
    'massif::CustomPopupHandler': {
    };
    'massif::CustomRasterTileLayer': {
        clearTileCaches: { args: [all: boolean]; result: void };
        refresh: { args: []; result: void };
    };
    'massif::DataSourceListener': {
    };
    'massif::DirAssetPackage': {
    };
    'massif::DouglasPeuckerGeometrySimplifier': {
    };
    'massif::DownloadTask': {
    };
    'massif::EPSG3857': {
    };
    'massif::EPSG4326': {
    };
    'massif::EditableVectorLayer': {
        refresh: { args: []; result: void };
    };
    'massif::ElevationDecoder': {
    };
    'massif::EventListener': {
    };
    'massif::Feature': {
    };
    'massif::FeatureBuilder': {
    };
    'massif::FeatureCollection': {
        getFeature: { args: [index: number]; result: Handle<'massif::Feature'>; resultClass: 'massif::Feature' };
    };
    'massif::FeatureCollectionSearchService': {
        findFeatures: { args: [request: Handle]; result: Handle<'massif::FeatureCollection'>; resultClass: 'massif::FeatureCollection' };
    };
    'massif::FetchTask': {
    };
    'massif::FetchTaskBase': {
    };
    'massif::FetchingTasks': {
    };
    'massif::FetchingTileTasks': {
    };
    'massif::FogOptions': {
    };
    'massif::GeoJSONGeometryReader': {
    };
    'massif::GeoJSONGeometryWriter': {
    };
    'massif::GeoJSONVectorTileDataSource': {
        addFeature: { args: [layer: number, feature: Json]; result: void };
        createLayer: { args: [name: string]; result: number };
        deleteLayer: { args: [layer: number]; result: void };
        getMetaDataElement: { args: [key: string]; result: Json };
        loadTile: { args: [tile: Tile]; result: Handle<'massif::TileData'>; resultClass: 'massif::TileData' };
        removeFeature: { args: [layer: number, id: Json]; result: void };
        setLayerGeoJSON: { args: [layer: number, geoJson: Json]; result: void };
        setMetaDataElement: { args: [key: string, value: Json]; result: void };
        updateFeature: { args: [layer: number, feature: Json]; result: void };
    };
    'massif::GeocodingAddress': {
    };
    'massif::GeocodingRequest': {
    };
    'massif::GeocodingResult': {
    };
    'massif::GeocodingService': {
        calculateAddresses: { args: [request: Handle]; result: Json };
    };
    'massif::Geometry': {
    };
    'massif::GeometryCollection': {
    };
    'massif::GeometryCollectionStyle': {
    };
    'massif::GeometryCollectionStyleBuilder': {
    };
    'massif::GeometrySimplifier': {
    };
    'massif::HTTPTileDataSource': {
        getMetaDataElement: { args: [key: string]; result: Json };
        loadTile: { args: [tile: Tile]; result: Handle<'massif::TileData'>; resultClass: 'massif::TileData' };
        setMetaDataElement: { args: [key: string, value: Json]; result: void };
    };
    'massif::HillshadeRasterTileLayer': {
        clearTileCaches: { args: [all: boolean]; result: void };
        getElevation: { args: [pos: Position]; result: number };
        getElevations: { args: [positions: Position[]]; result: number[] };
        refresh: { args: []; result: void };
    };
    'massif::Label': {
    };
    'massif::LabelStyle': {
    };
    'massif::LabelStyleBuilder': {
    };
    'massif::Layer': {
        refresh: { args: []; result: void };
    };
    'massif::Layers': {
        add: { args: [layer: Handle]; result: void };
        clear: { args: []; result: void };
        get: { args: [index: number]; result: Handle<'massif::Layer'>; resultClass: 'massif::Layer' };
        insert: { args: [index: number, layer: Handle]; result: void };
        remove: { args: [layer: Handle]; result: boolean };
        set: { args: [index: number, layer: Handle]; result: void };
    };
    'massif::LightOptions': {
        setSunPositionFromTime: { args: [year: number, month: number, day: number, hour: number, minute: number, latitude: number, longitude: number]; result: void };
    };
    'massif::LightStop': {
    };
    'massif::Line': {
    };
    'massif::LineGeometry': {
    };
    'massif::LineStyle': {
    };
    'massif::LineStyleBuilder': {
    };
    'massif::LocalVectorDataSource': {
        add: { args: [element: Handle]; result: void };
        clear: { args: []; result: void };
        remove: { args: [element: Handle]; result: boolean };
    };
    'massif::Log': {
    };
    'massif::LogEventListener': {
    };
    'massif::MBTilesTileDataSource': {
        getMetaDataElement: { args: [key: string]; result: Json };
        loadTile: { args: [tile: Tile]; result: Handle<'massif::TileData'>; resultClass: 'massif::TileData' };
        setMetaDataElement: { args: [key: string, value: Json]; result: void };
    };
    'massif::MBVectorTileDecoder': {
        addFallbackFont: { args: [font: Handle]; result: void };
        getLegend: { args: [spec: Json]; result: Json };
        getStyleParameter: { args: [name: string]; result: string };
        setStyleParameter: { args: [name: string, value: string]; result: boolean };
        setStyleParameters: { args: [params: Json]; result: void };
    };
    'massif::ManeuverArrowBuilder': {
    };
    'massif::MapBounds': {
    };
    'massif::MapBoxElevationDataDecoder': {
    };
    'massif::MapBoxOnlineGeocodingService': {
        calculateAddresses: { args: [request: Handle]; result: Json };
    };
    'massif::MapBoxOnlineReverseGeocodingService': {
        calculateAddresses: { args: [request: Handle]; result: Json };
    };
    'massif::MapClickInfo': {
    };
    'massif::MapEnvelope': {
    };
    'massif::MapEventListener': {
    };
    'massif::MapInteractionInfo': {
    };
    'massif::MapMoveInfo': {
    };
    'massif::MapPos': {
    };
    'massif::MapRange': {
    };
    'massif::MapRenderer': {
    };
    'massif::MapRendererListener': {
    };
    'massif::MapTile': {
    };
    'massif::MapTilerOnlineTileDataSource': {
        getMetaDataElement: { args: [key: string]; result: Json };
        loadTile: { args: [tile: Tile]; result: Handle<'massif::TileData'>; resultClass: 'massif::TileData' };
        setMetaDataElement: { args: [key: string, value: Json]; result: void };
    };
    'massif::MapVec': {
    };
    'massif::Marker': {
    };
    'massif::MarkerStyle': {
    };
    'massif::MarkerStyleBuilder': {
    };
    'massif::MassifApi': {
    };
    'massif::MassifInterop': {
    };
    'massif::MemoryCacheTileDataSource': {
        getMetaDataElement: { args: [key: string]; result: Json };
        loadTile: { args: [tile: Tile]; result: Handle<'massif::TileData'>; resultClass: 'massif::TileData' };
        setMetaDataElement: { args: [key: string, value: Json]; result: void };
    };
    'massif::MergedMBVTTileDataSource': {
        getMetaDataElement: { args: [key: string]; result: Json };
        loadTile: { args: [tile: Tile]; result: Handle<'massif::TileData'>; resultClass: 'massif::TileData' };
        setMetaDataElement: { args: [key: string, value: Json]; result: void };
    };
    'massif::MultiGeometry': {
    };
    'massif::MultiLineGeometry': {
    };
    'massif::MultiOSMOfflineGeocodingService': {
        add: { args: [database: string]; result: void };
        calculateAddresses: { args: [request: Handle]; result: Json };
        remove: { args: [database: string]; result: boolean };
    };
    'massif::MultiOSMOfflineReverseGeocodingService': {
        add: { args: [database: string]; result: void };
        calculateAddresses: { args: [request: Handle]; result: Json };
        remove: { args: [database: string]; result: boolean };
    };
    'massif::MultiPointGeometry': {
    };
    'massif::MultiPolygonGeometry': {
    };
    'massif::MultiTileDataSource': {
        add: { args: [datasource: Handle, tileMask: string]; result: void };
        getMetaDataElement: { args: [key: string]; result: Json };
        loadTile: { args: [tile: Tile]; result: Handle<'massif::TileData'>; resultClass: 'massif::TileData' };
        remove: { args: [datasource: Handle]; result: boolean };
        setMetaDataElement: { args: [key: string, value: Json]; result: void };
    };
    'massif::MultiValhallaOfflineRoutingService': {
        add: { args: [database: string]; result: void };
        addLocale: { args: [key: string, json: string]; result: void };
        calculateRoute: { args: [request: Handle]; result: Handle<'massif::RoutingResult'>; resultClass: 'massif::RoutingResult' };
        matchRoute: { args: [request: Handle]; result: Handle<'massif::RouteMatchingResult'>; resultClass: 'massif::RouteMatchingResult' };
        remove: { args: [database: string]; result: boolean };
        setConfigurationParameter: { args: [param: string, value: Json]; result: void };
    };
    'massif::NMLModel': {
    };
    'massif::NMLModelStyle': {
    };
    'massif::NMLModelStyleBuilder': {
    };
    'massif::OSMOfflineGeocodingService': {
        calculateAddresses: { args: [request: Handle]; result: Json };
    };
    'massif::OSMOfflineReverseGeocodingService': {
        calculateAddresses: { args: [request: Handle]; result: Json };
    };
    'massif::OSRMOfflineRoutingService': {
        calculateRoute: { args: [request: Handle]; result: Handle<'massif::RoutingResult'>; resultClass: 'massif::RoutingResult' };
        matchRoute: { args: [request: Handle]; result: Handle<'massif::RouteMatchingResult'>; resultClass: 'massif::RouteMatchingResult' };
    };
    'massif::OnChangeListener': {
    };
    'massif::Options': {
    };
    'massif::OptionsListener': {
    };
    'massif::OrderedTileDataSource': {
        getMetaDataElement: { args: [key: string]; result: Json };
        loadTile: { args: [tile: Tile]; result: Handle<'massif::TileData'>; resultClass: 'massif::TileData' };
        setMetaDataElement: { args: [key: string, value: Json]; result: void };
    };
    'massif::PMTilesTileDataSource': {
        getMetaDataElement: { args: [key: string]; result: Json };
        loadTile: { args: [tile: Tile]; result: Handle<'massif::TileData'>; resultClass: 'massif::TileData' };
        setMetaDataElement: { args: [key: string, value: Json]; result: void };
    };
    'massif::PackageInfo': {
    };
    'massif::PackageManager': {
    };
    'massif::PackageManagerGeocodingService': {
        calculateAddresses: { args: [request: Handle]; result: Json };
    };
    'massif::PackageManagerListener': {
    };
    'massif::PackageManagerReverseGeocodingService': {
        calculateAddresses: { args: [request: Handle]; result: Json };
    };
    'massif::PackageManagerRoutingService': {
        calculateRoute: { args: [request: Handle]; result: Handle<'massif::RoutingResult'>; resultClass: 'massif::RoutingResult' };
        matchRoute: { args: [request: Handle]; result: Handle<'massif::RouteMatchingResult'>; resultClass: 'massif::RouteMatchingResult' };
    };
    'massif::PackageManagerTileDataSource': {
        getMetaDataElement: { args: [key: string]; result: Json };
        loadTile: { args: [tile: Tile]; result: Handle<'massif::TileData'>; resultClass: 'massif::TileData' };
        setMetaDataElement: { args: [key: string, value: Json]; result: void };
    };
    'massif::PackageManagerValhallaRoutingService': {
        calculateRoute: { args: [request: Handle]; result: Handle<'massif::RoutingResult'>; resultClass: 'massif::RoutingResult' };
        matchRoute: { args: [request: Handle]; result: Handle<'massif::RouteMatchingResult'>; resultClass: 'massif::RouteMatchingResult' };
    };
    'massif::PackageMetaInfo': {
    };
    'massif::PackageStatus': {
    };
    'massif::PackageTileMask': {
    };
    'massif::PeliasOnlineGeocodingService': {
        calculateAddresses: { args: [request: Handle]; result: Json };
    };
    'massif::PeliasOnlineReverseGeocodingService': {
        calculateAddresses: { args: [request: Handle]; result: Json };
    };
    'massif::PersistentCacheTileDataSource': {
        clear: { args: []; result: void };
        getMetaDataElement: { args: [key: string]; result: Json };
        loadTile: { args: [tile: Tile]; result: Handle<'massif::TileData'>; resultClass: 'massif::TileData' };
        setMetaDataElement: { args: [key: string, value: Json]; result: void };
        startDownloadArea: { args: [bounds: Json, minZoom: number, maxZoom: number, fetchDelay: number]; result: void };
        stopAllDownloads: { args: []; result: void };
    };
    'massif::PersistentTaskQueue': {
    };
    'massif::Point': {
    };
    'massif::PointDetailTileDataSource': {
        getMetaDataElement: { args: [key: string]; result: Json };
        loadTile: { args: [tile: Tile]; result: Handle<'massif::TileData'>; resultClass: 'massif::TileData' };
        setMetaDataElement: { args: [key: string, value: Json]; result: void };
    };
    'massif::PointGeometry': {
    };
    'massif::PointStyle': {
    };
    'massif::PointStyleBuilder': {
    };
    'massif::Polygon': {
    };
    'massif::Polygon3D': {
    };
    'massif::Polygon3DStyle': {
    };
    'massif::Polygon3DStyleBuilder': {
    };
    'massif::PolygonGeometry': {
    };
    'massif::PolygonStyle': {
    };
    'massif::PolygonStyleBuilder': {
    };
    'massif::Popup': {
    };
    'massif::PopupClickInfo': {
    };
    'massif::PopupDrawInfo': {
    };
    'massif::PopupStyle': {
    };
    'massif::PopupStyleBuilder': {
    };
    'massif::PostProcessEffect': {
        setFloatParameter: { args: [name: string, value: number]; result: void };
    };
    'massif::Projection': {
    };
    'massif::RasterTileClickInfo': {
    };
    'massif::RasterTileEventListener': {
    };
    'massif::RasterTileLayer': {
        clearTileCaches: { args: [all: boolean]; result: void };
        refresh: { args: []; result: void };
    };
    'massif::RedrawRequestListener': {
    };
    'massif::RendererCaptureListener': {
    };
    'massif::ReverseGeocodingRequest': {
    };
    'massif::ReverseGeocodingService': {
        calculateAddresses: { args: [request: Handle]; result: Json };
    };
    'massif::RouteMatchingEdge': {
    };
    'massif::RouteMatchingPoint': {
    };
    'massif::RouteMatchingRequest': {
        setCustomParameter: { args: [name: string, value: Json]; result: void };
    };
    'massif::RouteMatchingResult': {
    };
    'massif::RoutingInstruction': {
    };
    'massif::RoutingRequest': {
        setCustomParameter: { args: [name: string, value: Json]; result: void };
    };
    'massif::RoutingResult': {
        getInstruction: { args: [index: number]; result: Handle<'massif::RoutingInstruction'>; resultClass: 'massif::RoutingInstruction' };
        getPoints: { args: []; result: number[] };
    };
    'massif::RoutingService': {
        calculateRoute: { args: [request: Handle]; result: Handle<'massif::RoutingResult'>; resultClass: 'massif::RoutingResult' };
        matchRoute: { args: [request: Handle]; result: Handle<'massif::RouteMatchingResult'>; resultClass: 'massif::RouteMatchingResult' };
    };
    'massif::SGREOfflineRoutingService': {
        calculateRoute: { args: [request: Handle]; result: Handle<'massif::RoutingResult'>; resultClass: 'massif::RoutingResult' };
        matchRoute: { args: [request: Handle]; result: Handle<'massif::RouteMatchingResult'>; resultClass: 'massif::RouteMatchingResult' };
    };
    'massif::ScreenBounds': {
    };
    'massif::ScreenPos': {
    };
    'massif::SearchRequest': {
    };
    'massif::SkyOptions': {
    };
    'massif::SolidLayer': {
        refresh: { args: []; result: void };
    };
    'massif::Style': {
    };
    'massif::StyleBuilder': {
    };
    'massif::TerrainOptions': {
        calculateHorizon: { args: [pos: Position, eyeHeight: number, azimuths: Json, maxDistance: number]; result: number[] };
        setSurfaceParameter: { args: [name: string, value: number]; result: void };
    };
    'massif::TerrariumElevationDataDecoder': {
    };
    'massif::Text': {
    };
    'massif::TextMargins': {
    };
    'massif::TextStyle': {
    };
    'massif::TextStyleBuilder': {
    };
    'massif::TileData': {
        getMetaDataElement: { args: [key: string]; result: Json };
    };
    'massif::TileDataSource': {
        getMetaDataElement: { args: [key: string]; result: Json };
        loadTile: { args: [tile: Tile]; result: Handle<'massif::TileData'>; resultClass: 'massif::TileData' };
        setMetaDataElement: { args: [key: string, value: Json]; result: void };
    };
    'massif::TileDecoderListener': {
    };
    'massif::TileDownloadInfo': {
    };
    'massif::TileDownloadListener': {
    };
    'massif::TileInfo': {
    };
    'massif::TileLayer': {
        clearTileCaches: { args: [all: boolean]; result: void };
        refresh: { args: []; result: void };
    };
    'massif::TileLoadListener': {
    };
    'massif::TileUtils': {
    };
    'massif::TomTomOnlineGeocodingService': {
        calculateAddresses: { args: [request: Handle]; result: Json };
    };
    'massif::TomTomOnlineReverseGeocodingService': {
        calculateAddresses: { args: [request: Handle]; result: Json };
    };
    'massif::TorqueTileDecoder': {
    };
    'massif::TorqueTileLayer': {
        clearTileCaches: { args: [all: boolean]; result: void };
        refresh: { args: []; result: void };
    };
    'massif::TouchHandlerListener': {
    };
    'massif::UTFGridClickInfo': {
    };
    'massif::UTFGridEventListener': {
    };
    'massif::UiDispatcher': {
    };
    'massif::ValhallaOfflineRoutingService': {
        calculateRoute: { args: [request: Handle]; result: Handle<'massif::RoutingResult'>; resultClass: 'massif::RoutingResult' };
        matchRoute: { args: [request: Handle]; result: Handle<'massif::RouteMatchingResult'>; resultClass: 'massif::RouteMatchingResult' };
    };
    'massif::ValhallaOnlineRoutingService': {
        calculateRoute: { args: [request: Handle]; result: Handle<'massif::RoutingResult'>; resultClass: 'massif::RoutingResult' };
        matchRoute: { args: [request: Handle]; result: Handle<'massif::RouteMatchingResult'>; resultClass: 'massif::RouteMatchingResult' };
    };
    'massif::Variant': {
    };
    'massif::VariantArrayBuilder': {
    };
    'massif::VariantObjectBuilder': {
    };
    'massif::VectorData': {
    };
    'massif::VectorDataSource': {
    };
    'massif::VectorEditEventListener': {
    };
    'massif::VectorElement': {
    };
    'massif::VectorElementClickInfo': {
    };
    'massif::VectorElementDragInfo': {
    };
    'massif::VectorElementEventListener': {
    };
    'massif::VectorElementSearchService': {
    };
    'massif::VectorLayer': {
        refresh: { args: []; result: void };
    };
    'massif::VectorTileClickInfo': {
    };
    'massif::VectorTileDecoder': {
    };
    'massif::VectorTileEventListener': {
    };
    'massif::VectorTileFeature': {
    };
    'massif::VectorTileFeatureBuilder': {
    };
    'massif::VectorTileFeatureCollection': {
        getFeature: { args: [index: number]; result: Handle<'massif::VectorTileFeature'>; resultClass: 'massif::VectorTileFeature' };
    };
    'massif::VectorTileLayer': {
        clearTileCaches: { args: [all: boolean]; result: void };
        refresh: { args: []; result: void };
    };
    'massif::VectorTileSearchService': {
        findFeatures: { args: [request: Handle]; result: Handle<'massif::VectorTileFeatureCollection'>; resultClass: 'massif::VectorTileFeatureCollection' };
    };
    'massif::ViewState': {
    };
    'massif::WKBGeometryReader': {
    };
    'massif::WKBGeometryWriter': {
    };
    'massif::WKTGeometryReader': {
    };
    'massif::WKTGeometryWriter': {
    };
    'massif::ZippedAssetPackage': {
    };
}

// --- events --------------------------------------------------------------

/** @internal A lookup table - see EventName and PayloadClass. */
export interface EventTypes {
    'massif::Address': {
    };
    'massif::AnimationStyle': {
    };
    'massif::AnimationStyleBuilder': {
    };
    'massif::AssetPackage': {
    };
    'massif::AssetTileDataSource': {
    };
    'massif::BalloonPopup': {
    };
    'massif::BalloonPopupButton': {
    };
    'massif::BalloonPopupButtonClickInfo': {
    };
    'massif::BalloonPopupButtonStyle': {
    };
    'massif::BalloonPopupButtonStyleBuilder': {
    };
    'massif::BalloonPopupEventListener': {
    };
    'massif::BalloonPopupMargins': {
    };
    'massif::BalloonPopupStyle': {
    };
    'massif::BalloonPopupStyleBuilder': {
    };
    'massif::BaseMapView': {
    };
    'massif::Billboard': {
    };
    'massif::BillboardStyle': {
    };
    'massif::BillboardStyleBuilder': {
    };
    'massif::BinaryData': {
    };
    'massif::Bitmap': {
    };
    'massif::BitmapOverlayRasterTileDataSource': {
    };
    'massif::BundleAssetPackage': {
    };
    'massif::CacheTileDataSource': {
    };
    'massif::CartoCSSStyleSet': {
    };
    'massif::CelestialArc': {
    };
    'massif::CelestialClickInfo': {
    };
    'massif::CelestialEventListener': {
    };
    'massif::CelestialLabel': {
    };
    'massif::CelestialLayer': {
        'celestial.clicked': 'massif::CelestialClickInfo';
    };
    'massif::CelestialObject': {
    };
    'massif::CelestialSprite': {
    };
    'massif::ClickInfo': {
    };
    'massif::ClusterElementBuilder': {
    };
    'massif::ClusterFetchTask': {
    };
    'massif::ClusteredVectorLayer': {
        'vectorelement.clicked': 'massif::VectorElementClickInfo';
    };
    'massif::Color': {
    };
    'massif::CombinedTileDataSource': {
    };
    'massif::CompiledStyleSet': {
    };
    'massif::CompositeVectorTileLayer': {
        'vectortile.clicked': 'massif::VectorTileClickInfo';
    };
    'massif::ContourTileDataSource': {
    };
    'massif::CullState': {
    };
    'massif::CustomPopup': {
    };
    'massif::CustomPopupHandler': {
    };
    'massif::CustomRasterTileLayer': {
    };
    'massif::DataSourceListener': {
    };
    'massif::DirAssetPackage': {
    };
    'massif::DouglasPeuckerGeometrySimplifier': {
    };
    'massif::DownloadTask': {
    };
    'massif::EPSG3857': {
    };
    'massif::EPSG4326': {
    };
    'massif::EditableVectorLayer': {
        'vectorelement.clicked': 'massif::VectorElementClickInfo';
    };
    'massif::ElevationDecoder': {
    };
    'massif::EventListener': {
    };
    'massif::Feature': {
    };
    'massif::FeatureBuilder': {
    };
    'massif::FeatureCollection': {
    };
    'massif::FeatureCollectionSearchService': {
    };
    'massif::FetchTask': {
    };
    'massif::FetchTaskBase': {
    };
    'massif::FetchingTasks': {
    };
    'massif::FetchingTileTasks': {
    };
    'massif::FogOptions': {
    };
    'massif::GeoJSONGeometryReader': {
    };
    'massif::GeoJSONGeometryWriter': {
    };
    'massif::GeoJSONVectorTileDataSource': {
    };
    'massif::GeocodingAddress': {
    };
    'massif::GeocodingRequest': {
    };
    'massif::GeocodingResult': {
    };
    'massif::GeocodingService': {
    };
    'massif::Geometry': {
    };
    'massif::GeometryCollection': {
    };
    'massif::GeometryCollectionStyle': {
    };
    'massif::GeometryCollectionStyleBuilder': {
    };
    'massif::GeometrySimplifier': {
    };
    'massif::HTTPTileDataSource': {
    };
    'massif::HillshadeRasterTileLayer': {
    };
    'massif::Label': {
    };
    'massif::LabelStyle': {
    };
    'massif::LabelStyleBuilder': {
    };
    'massif::Layer': {
    };
    'massif::Layers': {
    };
    'massif::LightOptions': {
    };
    'massif::LightStop': {
    };
    'massif::Line': {
    };
    'massif::LineGeometry': {
    };
    'massif::LineStyle': {
    };
    'massif::LineStyleBuilder': {
    };
    'massif::LocalVectorDataSource': {
    };
    'massif::Log': {
    };
    'massif::LogEventListener': {
    };
    'massif::MBTilesTileDataSource': {
    };
    'massif::MBVectorTileDecoder': {
    };
    'massif::ManeuverArrowBuilder': {
    };
    'massif::MapBounds': {
    };
    'massif::MapBoxElevationDataDecoder': {
    };
    'massif::MapBoxOnlineGeocodingService': {
    };
    'massif::MapBoxOnlineReverseGeocodingService': {
    };
    'massif::MapClickInfo': {
    };
    'massif::MapEnvelope': {
    };
    'massif::MapEventListener': {
    };
    'massif::MapInteractionInfo': {
    };
    'massif::MapMoveInfo': {
    };
    'massif::MapPos': {
    };
    'massif::MapRange': {
    };
    'massif::MapRenderer': {
    };
    'massif::MapRendererListener': {
    };
    'massif::MapTile': {
    };
    'massif::MapTilerOnlineTileDataSource': {
    };
    'massif::MapVec': {
    };
    'massif::Marker': {
    };
    'massif::MarkerStyle': {
    };
    'massif::MarkerStyleBuilder': {
    };
    'massif::MassifApi': {
    };
    'massif::MassifInterop': {
    };
    'massif::MemoryCacheTileDataSource': {
    };
    'massif::MergedMBVTTileDataSource': {
    };
    'massif::MultiGeometry': {
    };
    'massif::MultiLineGeometry': {
    };
    'massif::MultiOSMOfflineGeocodingService': {
    };
    'massif::MultiOSMOfflineReverseGeocodingService': {
    };
    'massif::MultiPointGeometry': {
    };
    'massif::MultiPolygonGeometry': {
    };
    'massif::MultiTileDataSource': {
    };
    'massif::MultiValhallaOfflineRoutingService': {
    };
    'massif::NMLModel': {
    };
    'massif::NMLModelStyle': {
    };
    'massif::NMLModelStyleBuilder': {
    };
    'massif::OSMOfflineGeocodingService': {
    };
    'massif::OSMOfflineReverseGeocodingService': {
    };
    'massif::OSRMOfflineRoutingService': {
    };
    'massif::OnChangeListener': {
    };
    'massif::Options': {
        'map.clicked': 'massif::MapClickInfo';
        'map.idle': null;
        'map.interaction': 'massif::MapInteractionInfo';
        'map.moved': 'massif::MapMoveInfo';
        'map.stable': 'massif::MapMoveInfo';
    };
    'massif::OptionsListener': {
    };
    'massif::OrderedTileDataSource': {
    };
    'massif::PMTilesTileDataSource': {
    };
    'massif::PackageInfo': {
    };
    'massif::PackageManager': {
    };
    'massif::PackageManagerGeocodingService': {
    };
    'massif::PackageManagerListener': {
    };
    'massif::PackageManagerReverseGeocodingService': {
    };
    'massif::PackageManagerRoutingService': {
    };
    'massif::PackageManagerTileDataSource': {
    };
    'massif::PackageManagerValhallaRoutingService': {
    };
    'massif::PackageMetaInfo': {
    };
    'massif::PackageStatus': {
    };
    'massif::PackageTileMask': {
    };
    'massif::PeliasOnlineGeocodingService': {
    };
    'massif::PeliasOnlineReverseGeocodingService': {
    };
    'massif::PersistentCacheTileDataSource': {
        'download.completed': null;
        'download.failed': 'massif::TileDownloadInfo';
        'download.progress': 'massif::TileDownloadInfo';
        'download.started': 'massif::TileDownloadInfo';
    };
    'massif::PersistentTaskQueue': {
    };
    'massif::Point': {
    };
    'massif::PointDetailTileDataSource': {
    };
    'massif::PointGeometry': {
    };
    'massif::PointStyle': {
    };
    'massif::PointStyleBuilder': {
    };
    'massif::Polygon': {
    };
    'massif::Polygon3D': {
    };
    'massif::Polygon3DStyle': {
    };
    'massif::Polygon3DStyleBuilder': {
    };
    'massif::PolygonGeometry': {
    };
    'massif::PolygonStyle': {
    };
    'massif::PolygonStyleBuilder': {
    };
    'massif::Popup': {
    };
    'massif::PopupClickInfo': {
    };
    'massif::PopupDrawInfo': {
    };
    'massif::PopupStyle': {
    };
    'massif::PopupStyleBuilder': {
    };
    'massif::PostProcessEffect': {
    };
    'massif::Projection': {
    };
    'massif::RasterTileClickInfo': {
    };
    'massif::RasterTileEventListener': {
    };
    'massif::RasterTileLayer': {
    };
    'massif::RedrawRequestListener': {
    };
    'massif::RendererCaptureListener': {
    };
    'massif::ReverseGeocodingRequest': {
    };
    'massif::ReverseGeocodingService': {
    };
    'massif::RouteMatchingEdge': {
    };
    'massif::RouteMatchingPoint': {
    };
    'massif::RouteMatchingRequest': {
    };
    'massif::RouteMatchingResult': {
    };
    'massif::RoutingInstruction': {
    };
    'massif::RoutingRequest': {
    };
    'massif::RoutingResult': {
    };
    'massif::RoutingService': {
    };
    'massif::SGREOfflineRoutingService': {
    };
    'massif::ScreenBounds': {
    };
    'massif::ScreenPos': {
    };
    'massif::SearchRequest': {
    };
    'massif::SkyOptions': {
    };
    'massif::SolidLayer': {
    };
    'massif::Style': {
    };
    'massif::StyleBuilder': {
    };
    'massif::TerrainOptions': {
    };
    'massif::TerrariumElevationDataDecoder': {
    };
    'massif::Text': {
    };
    'massif::TextMargins': {
    };
    'massif::TextStyle': {
    };
    'massif::TextStyleBuilder': {
    };
    'massif::TileData': {
    };
    'massif::TileDataSource': {
    };
    'massif::TileDecoderListener': {
    };
    'massif::TileDownloadInfo': {
    };
    'massif::TileDownloadListener': {
    };
    'massif::TileInfo': {
    };
    'massif::TileLayer': {
    };
    'massif::TileLoadListener': {
    };
    'massif::TileUtils': {
    };
    'massif::TomTomOnlineGeocodingService': {
    };
    'massif::TomTomOnlineReverseGeocodingService': {
    };
    'massif::TorqueTileDecoder': {
    };
    'massif::TorqueTileLayer': {
        'vectortile.clicked': 'massif::VectorTileClickInfo';
    };
    'massif::TouchHandlerListener': {
    };
    'massif::UTFGridClickInfo': {
    };
    'massif::UTFGridEventListener': {
    };
    'massif::UiDispatcher': {
    };
    'massif::ValhallaOfflineRoutingService': {
    };
    'massif::ValhallaOnlineRoutingService': {
    };
    'massif::Variant': {
    };
    'massif::VariantArrayBuilder': {
    };
    'massif::VariantObjectBuilder': {
    };
    'massif::VectorData': {
    };
    'massif::VectorDataSource': {
    };
    'massif::VectorEditEventListener': {
    };
    'massif::VectorElement': {
    };
    'massif::VectorElementClickInfo': {
    };
    'massif::VectorElementDragInfo': {
    };
    'massif::VectorElementEventListener': {
    };
    'massif::VectorElementSearchService': {
    };
    'massif::VectorLayer': {
        'vectorelement.clicked': 'massif::VectorElementClickInfo';
    };
    'massif::VectorTileClickInfo': {
    };
    'massif::VectorTileDecoder': {
    };
    'massif::VectorTileEventListener': {
    };
    'massif::VectorTileFeature': {
    };
    'massif::VectorTileFeatureBuilder': {
    };
    'massif::VectorTileFeatureCollection': {
    };
    'massif::VectorTileLayer': {
        'vectortile.clicked': 'massif::VectorTileClickInfo';
    };
    'massif::VectorTileSearchService': {
    };
    'massif::ViewState': {
    };
    'massif::WKBGeometryReader': {
    };
    'massif::WKBGeometryWriter': {
    };
    'massif::WKTGeometryReader': {
    };
    'massif::WKTGeometryWriter': {
    };
    'massif::ZippedAssetPackage': {
    };
}
