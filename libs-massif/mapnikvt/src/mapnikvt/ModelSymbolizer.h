#ifndef _MASSIF_MAPNIKVT_MODELSYMBOLIZER_H_
#define _MASSIF_MAPNIKVT_MODELSYMBOLIZER_H_

#include "Symbolizer.h"

namespace massif::mvt {
    // A glTF model at each point, instanced by the renderer. Every per-instance property may read
    // random(): it is evaluated per point, so each point of a packed MultiPoint gets its own value.
    class ModelSymbolizer : public Symbolizer {
    public:
        explicit ModelSymbolizer(std::shared_ptr<Logger> logger) : Symbolizer(std::move(logger)) {
            unbindProperty("comp-op");
            bindProperty("file", &_file, true);
            bindProperty("scale", &_scale);
            bindProperty("rotation", &_rotation);
            bindProperty("z-offset", &_zOffset);
            bindProperty("color", &_color);
            bindProperty("color-mix", &_colorMix);
            bindProperty("opacity", &_opacity);
            bindProperty("cast-shadows", &_castShadows);
            bindProperty("spin", &_spin);
            bindProperty("spin-axis", &_spinAxis);
            bindProperty("allow-density-reduction", &_allowDensityReduction);
            bindProperty("allow-overlap", &_allowOverlap);
        }

        virtual FeatureProcessor createFeatureProcessor(const ExpressionContext& exprContext, const SymbolizerContext& symbolizerContext) const override;

        virtual std::string getTypeName() const override { return "model"; }

        // Placement against the overlap rule sees every point of the source tile, so it decides the
        // same way whichever overzoomed tile is cut from it.
        virtual bool needsUnclippedFeatures() const override { return true; }

        // [mapnik::seed] for a point: its world position, so the value survives re-cutting the tile.
        static long long instanceSeed(const vt::TileId& tileId, const cglib::vec2<float>& vertex);

    protected:
        StringProperty _file = StringProperty("");
        FloatProperty _scale = FloatProperty(1.0f);
        FloatProperty _rotation = FloatProperty(0.0f); // degrees clockwise from north
        FloatProperty _zOffset = FloatProperty(0.0f);  // metres
        ColorProperty _color = ColorProperty("#ffffff");
        FloatProperty _colorMix = FloatProperty(0.0f);
        FloatFunctionProperty _opacity = FloatFunctionProperty(1.0f);
        BoolProperty _castShadows = BoolProperty(true);
        FloatProperty _spin = FloatProperty(0.0f);     // revolutions per minute
        StringProperty _spinAxis = StringProperty("y"); // the model's own axis, map frame: x east, y north (glTF -z), z up
        BoolProperty _allowDensityReduction = BoolProperty(true);
        BoolProperty _allowOverlap = BoolProperty(false);
    };
}

#endif
