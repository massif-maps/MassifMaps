#include "ModelSymbolizer.h"
#include "ExpressionUtils.h"

#include <cmath>
#include <map>

namespace massif::mvt {
    long long ModelSymbolizer::instanceSeed(const vt::TileId& tileId, const cglib::vec2<float>& vertex) {
        return vt::modelPointSeed(tileId, vertex);
    }

    ModelSymbolizer::FeatureProcessor ModelSymbolizer::createFeatureProcessor(const ExpressionContext& exprContext, const SymbolizerContext& symbolizerContext) const {
        std::shared_ptr<vt::ModelManager> modelManager = symbolizerContext.getModelManager();
        vt::FloatFunction opacityFunc = _opacity.getFunction(exprContext);
        if (!modelManager || opacityFunc == vt::FloatFunction(0)) {
            return FeatureProcessor();
        }

        vt::ModelStyle baseStyle;
        baseStyle.opacityFunc = opacityFunc;
        baseStyle.castShadows = _castShadows.getValue(exprContext);
        baseStyle.densityReduction = _allowDensityReduction.getValue(exprContext);
        baseStyle.allowOverlap = _allowOverlap.getValue(exprContext);
        baseStyle.spinSpeed = _spin.getValue(exprContext) * 3.14159265f / 30.0f;
        std::string spinAxis = _spinAxis.getValue(exprContext);
        baseStyle.spinAxis = spinAxis == "x" ? cglib::vec3<float>(1, 0, 0) : spinAxis == "z" ? cglib::vec3<float>(0, 0, 1) : cglib::vec3<float>(0, 1, 0);

        // Only the properties reading the seed need a pass per point; the rest are fixed per feature.
        bool perInstance = false;
        for (const Property* property : std::initializer_list<const Property*> { &_file, &_scale, &_rotation, &_zOffset, &_color, &_colorMix }) {
            std::visit(ExpressionVariableVisitor([&perInstance](const std::shared_ptr<VariableExpression>& varExpr) {
                if (auto val = std::get_if<Value>(&varExpr->getVariableExpression())) {
                    perInstance = perInstance || ValueConverter<std::string>::convert(*val) == "mapnik::seed";
                }
            }), property->getExpression());
        }

        return [modelManager, baseStyle, perInstance, context = ExpressionContext(exprContext), this](const FeatureCollection& featureCollection, vt::TileLayerBuilder& layerBuilder) mutable {
            std::map<std::string, vt::TileLayerBuilder::ModelProcessor> processors;
            auto evaluate = [&](vt::ModelInstance& instance, std::string& file) {
                file = _file.getValue(context);
                instance.scale = _scale.getValue(context);
                instance.rotation = _rotation.getValue(context) * 3.14159265f / 180.0f;
                instance.zOffset = _zOffset.getValue(context);
                instance.color = _color.getValue(context);
                instance.colorMix = _colorMix.getValue(context);
            };
            vt::ModelInstance featureInstance;
            std::string featureFile;
            if (!perInstance) {
                evaluate(featureInstance, featureFile);
            }

            bool suppressWarning = false;
            for (std::size_t featureIndex = 0; featureIndex < featureCollection.size(); featureIndex++) {
                auto pointGeometry = std::get_if<PointGeometry>(featureCollection.getGeometry(featureIndex).get());
                if (!pointGeometry) {
                    if (!suppressWarning) {
                        _logger->write(Logger::Severity::WARNING, "Unsupported geometry for ModelSymbolizer");
                        suppressWarning = true;
                    }
                    continue;
                }
                const PointGeometry::VerticesList& verticesList = pointGeometry->getVerticesList();
                for (std::size_t part = 0; part < verticesList.size(); part++) {
                    for (const cglib::vec2<float>& vertex : verticesList[part]) {
                        if (!layerBuilder.isModelPointInReach(vertex)) {
                            continue;
                        }
                        long long seed = instanceSeed(context.getTileId(), vertex);
                        vt::ModelInstance instance = featureInstance;
                        std::string file = featureFile;
                        if (perInstance) {
                            context.setInstanceSeed(seed);
                            evaluate(instance, file);
                            context.setInstanceSeed(std::nullopt);
                        }
                        instance.spinPhase = static_cast<float>(static_cast<unsigned long long>(seed) % 6283) * 0.001f;

                        auto it = processors.find(file);
                        if (it == processors.end()) {
                            vt::ModelStyle style = baseStyle;
                            style.model = file.empty() ? std::shared_ptr<const vt::Model>() : modelManager->loadModel(file);
                            it = processors.emplace(file, style.model ? layerBuilder.createModelProcessor(style) : vt::TileLayerBuilder::ModelProcessor()).first;
                        }
                        if (it->second) {
                            it->second(featureCollection.getLocalId(featureIndex), featureCollection.getFeatureId(featureIndex), vertex, instance, pointGeometry->getPartIndex(part));
                        }
                    }
                }
            }
        };
    }
}
