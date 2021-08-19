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
            } else if (d > outMax) {
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
    LabelCuller::LabelCuller(float scale) :
        _localCameraProjMatrix(cglib::mat4x4<float>::identity()), _scale(scale), _mutex()
    {
    }

    void LabelCuller::setViewState(const ViewState& viewState) {
        std::lock_guard<std::mutex> lock(_mutex);

        cglib::mat4x4<double> localCameraMatrix = viewState.cameraMatrix;
        for (int i = 0; i < 3; i++) {
            localCameraMatrix(i, 3) = 0;
        }
        _localCameraProjMatrix = cglib::mat4x4<float>::convert(viewState.projectionMatrix * localCameraMatrix);
        _viewState = viewState;
        _viewState.zoomScale *= _scale;
    }

    void LabelCuller::reset() {
        std::lock_guard<std::mutex> lock(_mutex);

        clearGrid();
    }

    bool LabelCuller::process(const std::vector<std::shared_ptr<Label>>& labelList, std::mutex& labelMutex) {
        struct LabelInfo {
            float priority;
            int layerIndex;
            float size;
            float opacity;
            std::shared_ptr<Label> label;
        };

        std::lock_guard<std::mutex> lock(_mutex);

        // Start by collecting valid labels and updating label placements
        std::vector<LabelInfo> validLabelList;
        validLabelList.reserve(labelList.size());
        for (const std::shared_ptr<Label>& label : labelList) {
            std::lock_guard<std::mutex> labelLock(labelMutex);
            
            // Analyze only active and valid labels
            if (!label->isActive()) {
                continue;
            }

            if (label->updatePlacement(_viewState)) {
                label->setOpacity(0);
            }
                
            if (label->isValid()) {
                validLabelList.push_back({ label->getPriority(), label->getLayerIndex(), label->getStyle()->sizeFunc(_viewState), label->getOpacity(), label });
            }
        }

        // Sort active labels by priority/size/opacity
        std::stable_sort(validLabelList.begin(), validLabelList.end(), [&](const LabelInfo& labelInfo1, const LabelInfo& labelInfo2) {
            if (labelInfo1.priority != labelInfo2.priority) {
                return labelInfo1.priority > labelInfo2.priority;
            }
            if (labelInfo1.layerIndex != labelInfo2.layerIndex) {
                return labelInfo1.layerIndex < labelInfo2.layerIndex;
            }
            if (labelInfo1.size != labelInfo2.size) {
                return labelInfo1.size > labelInfo2.size;
            }
            if (labelInfo1.opacity != labelInfo2.opacity) {
                return labelInfo1.opacity > labelInfo2.opacity;
            }
            return labelInfo1.label->getGlobalId() > labelInfo2.label->getGlobalId();
        });

        // Update label visibility flag based on overlap analysis
        std::unordered_map<long long, std::vector<std::shared_ptr<Label>>> groupMap;
        groupMap.reserve(validLabelList.size());
        bool changed = false;
        for (const LabelInfo& labelInfo : validLabelList) {
            std::lock_guard<std::mutex> labelLock(labelMutex);

            const std::shared_ptr<Label>& label = labelInfo.label;

            // Label is always visible if its group is set to negative value. Otherwise test visibility against other labels
            bool visible = label->getGroupId() < 0 || testOverlap(label);
            if (visible && label->getGroupId() > 0) {
                for (const std::shared_ptr<Label>& otherLabel : groupMap[label->getGroupId()]) {
                    float minimumDistance = std::min(label->getMinimumGroupDistance(), otherLabel->getMinimumGroupDistance());

                    // Do the test between labels by enlarging the envelope of the label by minimum allowed distance and then by comparing bounds of envelopes.
                    std::array<cglib::vec2<float>, 4> envelope1, envelope2;
                    if (calculateScreenEnvelope(label, minimumDistance * 0.5f, envelope1) && calculateScreenEnvelope(otherLabel, minimumDistance * 0.5f, envelope2)) {
                        cglib::bbox2<float> bounds1 = cglib::bbox2<float>::make_union(envelope1.begin(), envelope1.end());
                        cglib::bbox2<float> bounds2 = cglib::bbox2<float>::make_union(envelope2.begin(), envelope2.end());
                        if (bounds1.inside(bounds2)) {
                            visible = false;
                            break;
                        }
                    }
                }

                if (visible) {
                    groupMap[label->getGroupId()].push_back(label);
                }
            }
            if (visible != label->isVisible()) {
                label->setVisible(visible);
                changed = true;
            }
        }
        return changed;
    }

    void LabelCuller::clearGrid() {
        for (int y = 0; y < GRID_RESOLUTION; y++) {
            for (int x = 0; x < GRID_RESOLUTION; x++) {
                _recordGrid[y][x].clear();
            }
        }
    }

    int LabelCuller::getGridIndex(float x) const {
        float v = x * 0.5f + 0.5f;
        if (v < 0) {
            return 0;
        }
        if (v >= 1) {
            return GRID_RESOLUTION - 1;
        }
        return static_cast<int>(v * GRID_RESOLUTION);
    }

    bool LabelCuller::testOverlap(const std::shared_ptr<Label>& label) {
        std::array<cglib::vec2<float>, 4> envelope;
        if (!calculateScreenEnvelope(label, 0, envelope)) {
            return false;
        }
        
        cglib::bbox2<float> bounds = cglib::bbox2<float>::make_union(envelope.begin(), envelope.end());
        int x0 = getGridIndex(bounds.min(0)), y0 = getGridIndex(bounds.min(1));
        int x1 = getGridIndex(bounds.max(0)), y1 = getGridIndex(bounds.max(1));
        for (int y = y0; y <= y1; y++) {
            for (int x = x0; x <= x1; x++) {
                for (const CullRecord& record : _recordGrid[y][x]) {
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
                _recordGrid[y][x].emplace_back(bounds, envelope);
            }
        }
        return true;
    }

    bool LabelCuller::calculateScreenEnvelope(const std::shared_ptr<Label>& label, float buffer, std::array<cglib::vec2<float>, 4>& envelope) const {
        std::array<cglib::vec3<float>, 4> mapEnvelope;
        if (!label->calculateEnvelope((label->getStyle()->sizeFunc)(_viewState), buffer, _viewState, mapEnvelope)) {
            return false;
        }
        
        for (int i = 0; i < 4; i++) {
            cglib::vec2<float> p_proj(cglib::proj_o(cglib::transform_point(mapEnvelope[i], _localCameraProjMatrix)));
            envelope[i] = p_proj;
        }
        return true;
    }
} }
