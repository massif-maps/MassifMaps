/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIFMAPS_H_
#define _MASSIFMAPS_H_

#import "MSFMassifApi.h"
// The facade sugar: hand-written, and not generated, so each new class needs a line here AND an
// entry in build-ios.py's extraHeaders, which is what copies it into the framework.
#import "api/MSFMassif.h"
#import "api/MSFMapEvents.h"
#import "api/MSFMassifObject.h"
#import "api/MSFMassifMap.h"
#import "MSFOptions.h"
#import "MSFLayers.h"

#ifdef _MASSIF_GEOCODING_SUPPORT
#import "MSFAddress.h"
#endif
#import "MSFMapBounds.h"
#import "MSFMapEnvelope.h"
#import "MSFMapPos.h"
#import "MSFScreenPos.h"
#import "MSFScreenBounds.h"
#import "MSFMapRange.h"
#import "MSFMapTile.h"
#import "MSFMapVec.h"
#import "MSFTileData.h"
#import "MSFVariant.h"
#import "MSFVariantArrayBuilder.h"
#import "MSFVariantObjectBuilder.h"

#import "MSFAssetTileDataSource.h"
#import "MSFCombinedTileDataSource.h"
#import "MSFOrderedTileDataSource.h"
#import "MSFMergedMBVTTileDataSource.h"
#import "MSFBitmapOverlayRasterTileDataSource.h"
#import "MSFGeoJSONVectorTileDataSource.h"
#import "MSFHTTPTileDataSource.h"
#import "MSFMemoryCacheTileDataSource.h"
#import "MSFMapTilerOnlineTileDataSource.h"
#import "MSFLocalVectorDataSource.h"
#import "MSFTileDownloadListener.h"
#import "MSFMultiTileDataSource.h"
#import "MSFPMTilesTileDataSource.h"
#import "MSFContourTileDataSource.h"

#import "MSFFeature.h"
#import "MSFFeatureCollection.h"
#import "MSFLineGeometry.h"
#import "MSFPointGeometry.h"
#import "MSFPolygonGeometry.h"
#import "MSFMultiGeometry.h"
#import "MSFMultiLineGeometry.h"
#import "MSFMultiPointGeometry.h"
#import "MSFMultiPolygonGeometry.h"
#import "MSFGeometrySimplifier.h"
#import "MSFDouglasPeuckerGeometrySimplifier.h"
#import "MSFGeoJSONGeometryReader.h"
#import "MSFGeoJSONGeometryWriter.h"

#import "MSFBitmap.h"
#import "MSFColor.h"
#import "MSFViewState.h"

#import "MSFManeuverArrowBuilder.h"

#import "MSFCelestialLayer.h"
#import "MSFCelestialSprite.h"
#import "MSFCelestialArc.h"

#import "MSFSolidLayer.h"
#import "MSFRasterTileEventListener.h"
#import "MSFRasterTileLayer.h"
#import "MSFHillshadeRasterTileLayer.h"
#import "MSFMapBoxElevationDataDecoder.h"
#import "MSFTerrariumElevationDataDecoder.h"
#import "MSFTileLoadListener.h"
#import "MSFUTFGridEventListener.h"
#import "MSFVectorElementEventListener.h"
#import "MSFVectorLayer.h"
#import "MSFVectorTileEventListener.h"
#import "MSFVectorTileLayer.h"
#import "MSFCompositeVectorTileLayer.h"
#import "MSFTorqueTileLayer.h"
#import "MSFClusteredVectorLayer.h"
#import "MSFClusterElementBuilder.h"

#import "MSFEPSG3857.h"
#import "MSFEPSG4326.h"

#import "MSFCullState.h"

#import "MSFAnimationStyleBuilder.h"
#import "MSFAnimationStyle.h"
#import "MSFBalloonPopupStyleBuilder.h"
#import "MSFBalloonPopupStyle.h"
#import "MSFBalloonPopupButtonStyleBuilder.h"
#import "MSFBalloonPopupButtonStyle.h"
#import "MSFLabelStyleBuilder.h"
#import "MSFLabelStyle.h"
#import "MSFLineStyleBuilder.h"
#import "MSFLineStyle.h"
#import "MSFMarkerStyleBuilder.h"
#import "MSFMarkerStyle.h"
#import "MSFPointStyleBuilder.h"
#import "MSFPointStyle.h"
#import "MSFPolygon3DStyleBuilder.h"
#import "MSFPolygon3DStyle.h"
#import "MSFPolygonStyleBuilder.h"
#import "MSFPolygonStyle.h"
#import "MSFPopupStyleBuilder.h"
#import "MSFPopupStyle.h"
#import "MSFTextStyleBuilder.h"
#import "MSFTextStyle.h"
#import "MSFNMLModelStyleBuilder.h"
#import "MSFNMLModelStyle.h"
#import "MSFGeometryCollectionStyle.h"
#import "MSFGeometryCollectionStyleBuilder.h"

#import "MSFMapRenderer.h"
#import "MSFMapRendererListener.h"
#import "MSFRendererCaptureListener.h"

#import "ui/MapView.h"
#import "MSFClickInfo.h"
#import "MSFMapClickInfo.h"
#import "MSFMapInteractionInfo.h"
#import "MSFMapEventListener.h"
#import "MSFBalloonPopupButtonClickInfo.h"
#import "MSFRasterTileClickInfo.h"
#import "MSFVectorTileClickInfo.h"
#import "MSFVectorElementClickInfo.h"

#import "MSFAssetUtils.h"
#import "MSFBitmapUtils.h"
#import "MSFTileUtils.h"
#import "MSFLog.h"
#import "MSFLogEventListener.h"
#import "utils/ExceptionWrapper.h"

#import "MSFBalloonPopup.h"
#import "MSFBalloonPopupButton.h"
#import "MSFBalloonPopupEventListener.h"
#import "MSFCustomPopup.h"
#import "MSFCustomPopupHandler.h"
#import "MSFGeometryCollection.h"
#import "MSFLabel.h"
#import "MSFLine.h"
#import "MSFMarker.h"
#import "MSFNMLModel.h"
#import "MSFPoint.h"
#import "MSFPolygon3D.h"
#import "MSFPolygon.h"
#import "MSFPopup.h"
#import "MSFText.h"

#import "MSFAssetPackage.h"
#import "MSFZippedAssetPackage.h"
#import "MSFDirAssetPackage.h"
#import "MSFCompiledStyleSet.h"
#import "MSFCartoCSSStyleSet.h"
#import "MSFVectorTileDecoder.h"
#import "MSFMBVectorTileDecoder.h"
#import "MSFTorqueTileDecoder.h"
#import "MSFVectorTileFeature.h"
#import "MSFVectorTileFeatureCollection.h"


#ifdef _MASSIF_OFFLINE_SUPPORT
#import "MSFMBTilesTileDataSource.h"
#import "MSFPersistentCacheTileDataSource.h"
#endif

#ifdef _MASSIF_PACKAGEMANAGER_SUPPORT
#import "MSFPackageManagerTileDataSource.h"

#ifdef _MASSIF_ROUTING_SUPPORT
#import "MSFPackageManagerRoutingService.h"
#ifdef _MASSIF_VALHALLA_ROUTING_SUPPORT
#import "MSFPackageManagerValhallaRoutingService.h"
#endif
#endif

#import "MSFPackageInfo.h"
#import "MSFPackageStatus.h"
#import "MSFPackageTileMask.h"
#import "MSFPackageManager.h"
#endif

#ifdef _MASSIF_GEOCODING_SUPPORT
#import "MSFGeocodingAddress.h"
#import "MSFGeocodingRequest.h"
#import "MSFGeocodingResult.h"
#import "MSFReverseGeocodingRequest.h"
#import "MSFGeocodingService.h"
#import "MSFReverseGeocodingService.h"
#import "MSFPackageManagerGeocodingService.h"
#import "MSFPackageManagerReverseGeocodingService.h"
#import "MSFOSMOfflineGeocodingService.h"
#import "MSFOSMOfflineReverseGeocodingService.h"
#import "MSFPeliasOnlineGeocodingService.h"
#import "MSFPeliasOnlineReverseGeocodingService.h"
#import "MSFMapBoxOnlineGeocodingService.h"
#import "MSFMapBoxOnlineReverseGeocodingService.h"
#import "MSFTomTomOnlineGeocodingService.h"
#import "MSFTomTomOnlineReverseGeocodingService.h"
#import "MSFMultiOSMOfflineGeocodingService.h"
#import "MSFMultiOSMOfflineReverseGeocodingService.h"
#endif

#ifdef _MASSIF_SEARCH_SUPPORT
#import "MSFSearchRequest.h"
#import "MSFFeatureCollectionSearchService.h"
#import "MSFVectorElementSearchService.h"
#import "MSFVectorTileSearchService.h"
#endif

#ifdef _MASSIF_ROUTING_SUPPORT
#import "MSFRoutingInstruction.h"
#import "MSFRoutingRequest.h"
#import "MSFRoutingResult.h"
#import "MSFRoutingService.h"
#import "MSFRouteMatchingRequest.h"
#import "MSFRouteMatchingResult.h"
#import "MSFOSRMOfflineRoutingService.h"
#import "MSFSGREOfflineRoutingService.h"
#import "MSFValhallaOnlineRoutingService.h"
#ifdef _MASSIF_VALHALLA_ROUTING_SUPPORT
#import "MSFMultiValhallaOfflineRoutingService.h"
#import "MSFValhallaOfflineRoutingService.h"
#endif
#endif

#ifdef _MASSIF_EDITABLE_SUPPORT
#import "MSFEditableVectorLayer.h"
#import "MSFVectorEditEventListener.h"
#endif

#ifdef _MASSIF_WKBT_SUPPORT
#import "MSFWKTGeometryReader.h"
#import "MSFWKTGeometryWriter.h"
#import "MSFWKBGeometryReader.h"
#import "MSFWKBGeometryWriter.h"
#endif

#endif
