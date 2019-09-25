/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_MAPNIKVT_FEATURE_H_
#define _CARTO_MAPNIKVT_FEATURE_H_

#include "Value.h"
#include "Geometry.h"

#include <memory>
#include <list>
#include <vector>
#include <unordered_set>
#include <map>

namespace carto { namespace mvt {
    class FeatureData final {
    public:
        enum class GeometryType {
            NULL_GEOMETRY = 0, POINT_GEOMETRY = 1, LINE_GEOMETRY = 2, POLYGON_GEOMETRY = 3
        };

        FeatureData() = delete;
        explicit FeatureData(GeometryType geomType, std::vector<std::pair<std::string, Value>> vars) : _geometryType(geomType), _variables(std::move(vars)) { }

        GeometryType getGeometryType() const { return _geometryType; }

        std::unordered_set<std::string> getVariableNames() const;

        bool getVariable(const std::string& name, Value& value) const;

    private:
        GeometryType _geometryType;
        std::vector<std::pair<std::string, Value>> _variables;
    };

    class Feature final {
    public:
        Feature() = default;
        explicit Feature(long long id, std::shared_ptr<const Geometry> geometry, std::shared_ptr<const FeatureData> featureData) : _id(id), _geometry(std::move(geometry)), _featureData(std::move(featureData)) { }

        long long getId() const { return _id; }
        const std::shared_ptr<const Geometry>& getGeometry() const { return _geometry; }
        const std::shared_ptr<const FeatureData>& getFeatureData() const { return _featureData; }

    private:
        long long _id = 0;
        std::shared_ptr<const Geometry> _geometry;
        std::shared_ptr<const FeatureData> _featureData;
    };
} }

#endif
