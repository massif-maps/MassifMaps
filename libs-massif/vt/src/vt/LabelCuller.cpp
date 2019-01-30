#include "LabelCuller.h"

#include <array>
#include <vector>
#include <list>
#include <unordered_map>
#include <memory>

#include <cglib/vec.h>
#include <cglib/mat.h>
#include <cglib/bbox.h>
#include <cglib/frustum3.h>

namespace {
    template <std::size_t N>
    static void gatherPolygonProjectionExtents(const std::array<cglib::vec2<float>, N>& vertList, const cglib::vec2<float>& v, float& outMin, float& outMax) {
        outMin = outMax = cglib::dot_product(v, vertList[0]);
        for (std::size_t i = 1; i < N; ++i) {
            float d = cglib::dot_product(v, vertList[i]);

            if (d < outMin) {
                outMin = d;
            }
            else if (d > outMax) {
                outMax = d;
            }
        }
    }

    template <std::size_t N1, std::size_t N2>
    static bool findSeparatingAxis(const std::array<cglib::vec2<float>, N1>& vertList1, const std::array<cglib::vec2<float>, N2>& vertList2) {
        std::size_t prev = N1 - 1;
        for (std::size_t cur = 0; cur < N1; ++cur) {
            cglib::vec2<float> edge = vertList1[cur] - vertList1[prev];
            cglib::vec2<float> v(edge(1), -edge(0));

            float min1, max1, min2, max2;
            gatherPolygonProjectionExtents(vertList1, v, min1, max1);
            gatherPolygonProjectionExtents(vertList2, v, min2, max2);
            if (max1 < min2 || max2 < min1) {
                return true;
            }

            prev = cur;
        }
        return false;
    }
}

namespace carto { namespace vt {
    LabelCuller::LabelCuller(std::shared_ptr<std::mutex> mutex, std::shared_ptr<const TileTransformer> transformer, float scale) :
        _projectionMatrix(cglib::mat4x4<double>::identity()), _mvpMatrix(cglib::mat4x4<float>::identity()), _viewState(cglib::mat4x4<double>::identity(), cglib::mat4x4<double>::identity(), 0, 1, 1, scale), _mutex(std::move(mutex)), _transformer(std::move(transformer)), _scale(scale)
    {
    }

    void LabelCuller::setViewState(const cglib::mat4x4<double>& projectionMatrix, const cglib::mat4x4<double>& cameraMatrix, float zoom, float aspectRatio, float resolution) {
        std::lock_guard<std::mutex> lock(*_mutex);

        _projectionMatrix = projectionMatrix;
        _mvpMatrix = cglib::mat4x4<float>::convert(projectionMatrix * calculateLocalViewMatrix(cameraMatrix));
        _viewState = ViewState(projectionMatrix, cameraMatrix, zoom, aspectRatio, resolution, _scale);
        _resolution = resolution;
    }

    void LabelCuller::process(const std::vector<std::shared_ptr<Label>>& labelList) {
        // Start by collecting valid labels and updating label placements
        std::vector<std::shared_ptr<Label>> validLabelList;
        validLabelList.reserve(labelList.size());
        for (const std::shared_ptr<Label>& label : labelList) {
            std::lock_guard<std::mutex> lock(*_mutex);
            
            // Analyze only active and valid labels
            if (!label->isActive()) {
                continue;
            }

            if (label->updatePlacement(_viewState)) {
                label->setOpacity(0);
            }
                
            if (label->isValid()) {
                validLabelList.push_back(label);
            }
        }

        // Sort active labels by priority/opacity
        {
            std::lock_guard<std::mutex> lock(*_mutex);
            std::sort(validLabelList.begin(), validLabelList.end(), [](const std::shared_ptr<Label>& label1, const std::shared_ptr<Label>& label2) {
                return std::pair<int, float>(-label1->getPriority(), label1->getOpacity()) > std::pair<int, float>(-label2->getPriority(), label2->getOpacity());
            });
        }

        // Update label visibility flag based on overlap analysis
        clearGrid();
        std::unordered_map<long long, std::vector<std::shared_ptr<Label>>> groupMap;
        for (const std::shared_ptr<Label>& label : validLabelList) {
            std::lock_guard<std::mutex> lock(*_mutex);

            // Label is always visible if its group is set to negative value. Otherwise test visibility against other labels
            bool visible = label->getGroupId() < 0 || testOverlap(label);
            if (visible && label->getGroupId() > 0) {
                cglib::vec3<double> center;
                if (!label->calculateCenter(center)) {
                    visible = false;
                }
                for (const std::shared_ptr<Label>& otherLabel : groupMap[label->getGroupId()]) {
                    cglib::vec3<double> otherCenter;
                    if (otherLabel->calculateCenter(otherCenter)) {
                        float minimumDistance = std::min(label->getMinimumGroupDistance(), otherLabel->getMinimumGroupDistance());
                        double centerDistance = cglib::length(center - otherCenter);
                        if (centerDistance * _resolution / _scale < minimumDistance) {
                            visible = false;
                            break;
                        }
                    }
                }
                if (visible) {
                    groupMap[label->getGroupId()].push_back(label);
                }
            }
            label->setVisible(visible);
        }
    }

    void LabelCuller::clearGrid() {
        for (int y = 0; y < GRID_RESOLUTION; y++) {
            for (int x = 0; x < GRID_RESOLUTION; x++) {
                _recordGrid[y][x].clear();
            }
        }
    }

    bool LabelCuller::testOverlap(const std::shared_ptr<Label>& label) {
        std::array<cglib::vec3<float>, 4> mapEnvelope;
        if (!label->calculateEnvelope(_viewState, mapEnvelope)) {
            return false;
        }

        std::array<cglib::vec2<float>, 4> envelope;
        cglib::bbox2<float> bounds = cglib::bbox2<float>::smallest();
        if (!label->getStyle()->translate) {
            for (int i = 0; i < 4; i++) {
                cglib::vec2<float> p_proj(cglib::proj_o(cglib::transform_point(mapEnvelope[i], _mvpMatrix)));
                envelope[i] = p_proj;
                bounds.add(p_proj);
            }
        }
        else {
            float zoomScale = std::exp2(label->getTileId().zoom - _viewState.zoom);
            cglib::vec2<float> translate = (*label->getStyle()->translate) * zoomScale;
            cglib::mat4x4<double> translateMatrix = cglib::mat4x4<double>::convert(_transformer->calculateTileTransform(label->getTileId(), translate, 1.0f));
            cglib::mat4x4<double> tileMatrix = _transformer->calculateTileMatrix(label->getTileId(), 1);
            cglib::mat4x4<float> mvpMatrix = cglib::mat4x4<float>::convert(_projectionMatrix * tileMatrix * translateMatrix * cglib::inverse(tileMatrix) * cglib::translate4_matrix(_viewState.origin));
            for (int i = 0; i < 4; i++) {
                cglib::vec2<float> p_proj(cglib::proj_o(cglib::transform_point(mapEnvelope[i], mvpMatrix)));
                envelope[i] = p_proj;
                bounds.add(p_proj);
            }
        }

        int x0 = getGridIndex(bounds.min(0)), y0 = getGridIndex(bounds.min(1));
        int x1 = getGridIndex(bounds.max(0)), y1 = getGridIndex(bounds.max(1));
        for (int y = y0; y <= y1; y++) {
            for (int x = x0; x <= x1; x++) {
                for (const Record& record : _recordGrid[y][x]) {
                    if (record.bounds.inside(bounds)) {
                        if (!findSeparatingAxis(record.envelope, envelope) && !findSeparatingAxis(envelope, record.envelope)) {
                            return false;
                        }
                    }
                }
            }
        }

        for (int y = y0; y <= y1; y++) {
            for (int x = x0; x <= x1; x++) {
                _recordGrid[y][x].emplace_back(bounds, envelope, label);
            }
        }
        return true;
    }

    int LabelCuller::getGridIndex(float x) {
        float v = x * 0.5f + 0.5f;
        if (v < 0) {
            return 0;
        }
        if (v >= 1) {
            return GRID_RESOLUTION - 1;
        }
        return static_cast<int>(v * GRID_RESOLUTION);
    }

    cglib::mat4x4<double> LabelCuller::calculateLocalViewMatrix(const cglib::mat4x4<double>& cameraMatrix) {
        cglib::mat4x4<double> mv = cameraMatrix;
        mv(0, 3) = mv(1, 3) = mv(2, 3) = 0;
        return mv;
    }
} }
