#ifndef _ANIMATEDRASTERTILELAYER_I
#define _ANIMATEDRASTERTILELAYER_I

%module AnimatedRasterTileLayer

!proxy_imports(massif::AnimatedRasterTileLayer, datasources.TileDataSource, layers.CustomRasterTileLayer, core.StringVector)

%{
#include "layers/AnimatedRasterTileLayer.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <std_string.i>
%include <std_vector.i>
%include <massifswig.i>

%import "core/StringVector.i"
%import "datasources/TileDataSource.i"
%import "layers/CustomRasterTileLayer.i"

!polymorphic_shared_ptr(massif::AnimatedRasterTileLayer, layers.AnimatedRasterTileLayer)

!spec(massif::AnimatedRasterTileLayer, layer, animated-raster, alias(source, dataSource))
%attributeval(massif::AnimatedRasterTileLayer, %arg(std::vector<std::string>), FrameTimestamps, getFrameTimestamps, setFrameTimestamps)
%attribute(massif::AnimatedRasterTileLayer, float, AnimationPosition, getAnimationPosition, setAnimationPosition)
%attribute(massif::AnimatedRasterTileLayer, int, PrefetchFrames, getPrefetchFrames, setPrefetchFrames)
%attribute(massif::AnimatedRasterTileLayer, int, PrefetchBudget, getPrefetchBudget, setPrefetchBudget)
%std_exceptions(massif::AnimatedRasterTileLayer::AnimatedRasterTileLayer)
%std_exceptions(massif::AnimatedRasterTileLayer::setFrameTimestamps)

%include "layers/AnimatedRasterTileLayer.h"

#endif
