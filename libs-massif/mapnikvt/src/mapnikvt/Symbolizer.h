/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_MAPNIKVT_SYMBOLIZER_H_
#define _MASSIF_MAPNIKVT_SYMBOLIZER_H_

#include "FeatureCollection.h"
#include "Expression.h"
#include "ExpressionContext.h"
#include "Properties.h"
#include "SymbolizerContext.h"
#include "Logger.h"
#include "vt/Color.h"
#include "vt/Transform.h"
#include "vt/TileLayerBuilder.h"

#include <memory>
#include <set>

namespace massif::mvt {
    class Symbolizer {
    public:
        using FeatureProcessor = std::function<void(const FeatureCollection& featureCollection, vt::TileLayerBuilder& layerBuilder)>;

        virtual ~Symbolizer() = default;

        std::set<std::string> getPropertyNames() const;
        Property* getProperty(const std::string& name);
        const Property* getProperty(const std::string& name) const;
        const bool hasProperties() const;
        bool isPropertyDefined(const std::string& name) const;

        virtual FeatureProcessor createFeatureProcessor(const ExpressionContext& exprContext, const SymbolizerContext& symbolizerContext) const = 0;

        // Whether the layer needs the shared extrusion anchor pass - see buildExtrusionAnchors.
        virtual bool needsExtrusionAnchors() const { return false; }

        // What a legend calls this symbolizer ("line", "text"...); empty for one that draws no feature.
        virtual std::string getTypeName() const { return std::string(); }

    protected:
        explicit Symbolizer(std::shared_ptr<Logger> logger) : _logger(std::move(logger)) {
            bindProperty("comp-op", &_compOp);
        }

        void bindProperty(const std::string& name, Property* prop, bool bakedAtDecode = false);
        void unbindProperty(const std::string& name);

        static long long convertId(const Value& val);
        static long long generateId();
        static long long combineId(long long id, std::size_t hash);
        /**
         * Folds a point label's anchor into its id, for a feature with no id (a tile gives them all id 0)
         * and for each point of a MultiPoint (they share one): the renderer merges labels of equal id.
         */
        static long long combineAnchorId(long long id, const vt::TileId& tileId, const cglib::vec2<float>& vertex);

        const std::shared_ptr<Logger> _logger;

        CompOpProperty _compOp = CompOpProperty("src-over");

    private:
        std::map<std::string, Property*> _propertyMap;
    };
}

#endif
