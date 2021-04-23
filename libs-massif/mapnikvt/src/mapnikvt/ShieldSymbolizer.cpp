#include "ShieldSymbolizer.h"

#include <vector>
#include <tuple>

namespace carto { namespace mvt {
    void ShieldSymbolizer::build(const FeatureCollection& featureCollection, const ExpressionContext& exprContext, const SymbolizerContext& symbolizerContext, vt::TileLayerBuilder& layerBuilder) const {
        std::shared_ptr<const vt::Font> font = getFont(symbolizerContext, exprContext);
        if (!font) {
            std::string faceName = _faceName.getValue(exprContext);
            std::string fontSetName = _fontSetName.getValue(exprContext);
            _logger->write(Logger::Severity::ERROR, "Failed to load shield font " + (!faceName.empty() ? faceName : fontSetName));
            return;
        }

        std::string file = _file.getValue(exprContext);
        std::shared_ptr<const vt::BitmapImage> backgroundBitmapImage = symbolizerContext.getBitmapManager()->loadBitmapImage(file, false, IMAGE_UPSAMPLING_SCALE);
        if (!backgroundBitmapImage || !backgroundBitmapImage->bitmap) {
            _logger->write(Logger::Severity::ERROR, "Failed to load shield bitmap " + file);
            return;
        }

        bool allowOverlap = _allowOverlap.getValue(exprContext);
        bool clip = _clip.isDefined() ? _clip.getValue(exprContext) : allowOverlap;
        float shieldDx = _shieldDx.getValue(exprContext);
        float shieldDy = _shieldDy.getValue(exprContext);

        float fontScale = symbolizerContext.getSettings().getFontScale();
        float bitmapSize = static_cast<float>(std::max(backgroundBitmapImage->bitmap->width, backgroundBitmapImage->bitmap->height)) * fontScale;
        vt::LabelOrientation placement = getPlacement(exprContext);
        vt::LabelOrientation orientation = placement;
        if (orientation == vt::LabelOrientation::LINE) {
            orientation = vt::LabelOrientation::BILLBOARD_2D; // shields should be billboards, even when placed on a line
        }
        float minimumDistance = (_minimumDistance.getValue(exprContext) + bitmapSize) * std::pow(2.0f, -exprContext.getAdjustedZoom()) / symbolizerContext.getSettings().getTileSize() * 2;
        float placementPriority = _placementPriority.getValue(exprContext);
        float orientationAngle = _orientationAngle.getValue(exprContext);
        float sizeStatic = _size.getStaticValue(exprContext);
        bool unlockImage = _unlockImage.getValue(exprContext);

        vt::TextFormatter textFormatter(font, sizeStatic, getFormatterOptions(symbolizerContext, exprContext));
        vt::TextFormatter::Options shieldFormatterOptions = textFormatter.getOptions();
        shieldFormatterOptions.offset = cglib::vec2<float>(shieldDx * fontScale, -shieldDy * fontScale);
        vt::TextFormatter shieldFormatter(font, sizeStatic, shieldFormatterOptions);
        vt::CompOp compOp = _compOp.getValue(exprContext);

        vt::ColorFunction fillFunc = _fillFuncBuilder.createColorOpacityFunction(_fill.getFunction(exprContext), _opacity.getFunction(exprContext));
        vt::FloatFunction sizeFunc = _sizeFuncBuilder.createScaledFloatFunction(_size.getFunction(exprContext), fontScale);
        vt::ColorFunction haloFillFunc = _haloFillFuncBuilder.createColorOpacityFunction(_haloFill.getFunction(exprContext), _haloOpacity.getFunction(exprContext));
        vt::FloatFunction haloRadiusFunc = _haloRadiusFuncBuilder.createScaledFloatFunction(_haloRadius.getFunction(exprContext), fontScale);

        std::vector<std::pair<long long, std::tuple<vt::TileLayerBuilder::Vertex, std::string>>> shieldInfos;
        std::vector<std::pair<long long, vt::TileLayerBuilder::TextLabelInfo>> labelInfos;

        auto addShield = [&](long long localId, long long globalId, const std::string& text, const std::optional<vt::TileLayerBuilder::Vertex>& vertex, const vt::TileLayerBuilder::Vertices& vertices) {
            long long groupId = (allowOverlap ? -1 : 1); // use separate group from markers, markers use group 0

            if (clip) {
                if (vertex) {
                    shieldInfos.emplace_back(localId, std::make_tuple(*vertex, text));
                }
                else if (!vertices.empty()) {
                    shieldInfos.emplace_back(localId, std::make_tuple(vertices.front(), text));
                }
            }
            else {
                labelInfos.emplace_back(localId, vt::TileLayerBuilder::TextLabelInfo(globalId * 3 + 2, groupId, text, vertex, vertices, placementPriority, minimumDistance));
            }
        };

        auto flushShields = [&]() {
            cglib::vec2<float> backgroundOffset;
            const vt::TextFormatter* formatter;
            if (unlockImage) {
                backgroundOffset = cglib::vec2<float>(-backgroundBitmapImage->bitmap->width * fontScale * 0.5f + shieldFormatterOptions.offset(0), -backgroundBitmapImage->bitmap->height * fontScale * 0.5f + shieldFormatterOptions.offset(1));
                formatter = &textFormatter;
            }
            else {
                backgroundOffset = cglib::vec2<float>(-backgroundBitmapImage->bitmap->width * fontScale * 0.5f, -backgroundBitmapImage->bitmap->height * fontScale * 0.5f);
                formatter = &shieldFormatter;
            }

            if (clip) {
                vt::TextStyle style(compOp, fillFunc, sizeFunc, haloFillFunc, haloRadiusFunc, orientationAngle, fontScale, backgroundOffset, backgroundBitmapImage);

                std::size_t textInfoIndex = 0;
                layerBuilder.addTexts([&](long long& id, vt::TileLayerBuilder::Vertex& vertex, std::string& text) {
                    if (textInfoIndex >= shieldInfos.size()) {
                        return false;
                    }
                    id = shieldInfos[textInfoIndex].first;
                    vertex = std::get<0>(shieldInfos[textInfoIndex].second);
                    text = std::get<1>(shieldInfos[textInfoIndex].second);
                    textInfoIndex++;
                    return true;
                }, style, *formatter);

                shieldInfos.clear();
            }
            else {
                vt::TextLabelStyle style(placement, fillFunc, sizeFunc, haloFillFunc, haloRadiusFunc, true, orientationAngle, fontScale, backgroundOffset, backgroundBitmapImage);

                std::size_t labelInfoIndex = 0;
                layerBuilder.addTextLabels([&](long long& id, vt::TileLayerBuilder::TextLabelInfo& labelInfo) {
                    if (labelInfoIndex >= labelInfos.size()) {
                        return false;
                    }
                    id = labelInfos[labelInfoIndex].first;
                    labelInfo = labelInfos[labelInfoIndex].second;
                    labelInfoIndex++;
                    return true;
                }, style, *formatter);

                labelInfos.clear();
            }
        };

        buildFeatureCollection(featureCollection, exprContext, symbolizerContext, shieldFormatter, placement, bitmapSize, addShield);

        flushShields();
    }
} }
