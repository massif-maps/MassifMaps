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
    static bool findSeparatingAxis(const std::array<cglib::vec2<float>, N1>& vertList1, const std::array<cglib::vec2<float>, N2>& vertList2, float buffer) {
        std::size_t prev = N1 - 1;
        for (std::size_t cur = 0; cur < N1; ++cur) {
            cglib::vec2<float> edge = vertList1[cur] - vertList1[prev];
            if (edge == cglib::vec2<float>::zero()) {
                continue;
            }
            cglib::vec2<float> v = cglib::unit(cglib::vec2<float>(edge(1), -edge(0)));

            float min1, max1, min2, max2;
            gatherPolygonProjectionExtents(vertList1, v, min1, max1);
            gatherPolygonProjectionExtents(vertList2, v, min2, max2);
            if (max1 + buffer < min2 || max2 + buffer < min1) {
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
            bool valid;
            float priority;
            int layerIndex;
            float size;
            float opacity;
            std::shared_ptr<Label> label;
            CullRecord cullRecord;
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
                CullRecord cullRecord;
                bool valid = calculateScreenEnvelope(label, cullRecord.envelope);
                cullRecord.bounds = cglib::bbox2<float>::make_union(cullRecord.envelope.begin(), cullRecord.envelope.end());
                validLabelList.push_back({ valid, label->getPriority(), label->getLayerIndex(), label->getStyle()->sizeFunc(_viewState), label->getOpacity(), label, cullRecord });
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
        std::unordered_map<long long, std::vector<const LabelInfo*>> groupMap;
        groupMap.reserve(validLabelList.size());
        bool changed = false;
        for (const LabelInfo& labelInfo : validLabelList) {
            std::lock_guard<std::mutex> labelLock(labelMutex);

            const std::shared_ptr<Label>& label = labelInfo.label;
            long long groupId = label->getGroupId();

            // Label is always visible if its group is set to negative value. Otherwise test visibility against other labels
            bool visible = groupId >= 0 ? labelInfo.valid && testGridOverlap(labelInfo.cullRecord) : labelInfo.valid;
            if (visible && groupId > 0) {
                for (const LabelInfo* otherLabelInfo : groupMap[groupId]) {
                    const std::shared_ptr<Label>& otherLabel = otherLabelInfo->label;

                    float minimumDistance = 2.0f * std::min(label->getMinimumGroupDistance(), otherLabel->getMinimumGroupDistance()) / _viewState.resolution;
                    if (!findSeparatingAxis(labelInfo.cullRecord.envelope, otherLabelInfo->cullRecord.envelope, minimumDistance) && !findSeparatingAxis(otherLabelInfo->cullRecord.envelope, labelInfo.cullRecord.envelope, minimumDistance)) {
                        visible = false;
                        break;
                    }
                }
            }

            if (visible) {
                if (groupId >= 0) {
                    addGridRecord(labelInfo.cullRecord);
                }
                if (groupId > 0) {
                    groupMap[groupId].push_back(&labelInfo);
                }
            }
            if (visible != label->isVisible()) {
                label->setVisible(visible);
                changed = true;
            }
        }
        return changed;
    }

    cglib::vec2<int> LabelCuller::getGridIndex(const cglib::vec2<float>& pos) const {
        int x = std::max(0, std::min(GRID_RESOLUTION_X - 1, static_cast<int>(GRID_RESOLUTION_X * (pos(0) * 0.5f + 0.5f))));
        int y = std::max(0, std::min(GRID_RESOLUTION_Y - 1, static_cast<int>(GRID_RESOLUTION_Y * (pos(1) * 0.5f + 0.5f))));
        return cglib::vec2<int>(x, y);
    }

    void LabelCuller::clearGrid() {
        for (int y = 0; y < GRID_RESOLUTION_Y; y++) {
            for (int x = 0; x < GRID_RESOLUTION_X; x++) {
                _recordGrid[y][x].clear();
            }
        }
    }

    void LabelCuller::addGridRecord(const CullRecord& cullRecord) {
        cglib::vec2<int> minPos = getGridIndex(cullRecord.bounds.min);
        cglib::vec2<int> maxPos = getGridIndex(cullRecord.bounds.max);
        for (int y = minPos(1); y <= maxPos(1); y++) {
            for (int x = minPos(0); x <= maxPos(0); x++) {
                _recordGrid[y][x].push_back(cullRecord);
            }
        }
    }

    bool LabelCuller::testGridOverlap(const CullRecord& cullRecord) const {
        cglib::vec2<int> minPos = getGridIndex(cullRecord.bounds.min);
        cglib::vec2<int> maxPos = getGridIndex(cullRecord.bounds.max);
        for (int y = minPos(1); y <= maxPos(1); y++) {
            for (int x = minPos(0); x <= maxPos(0); x++) {
                for (const CullRecord& otherCullRecord : _recordGrid[y][x]) {
                    if (otherCullRecord.bounds.inside(cullRecord.bounds)) {
                        if (!findSeparatingAxis(otherCullRecord.envelope, cullRecord.envelope, 0) && !findSeparatingAxis(cullRecord.envelope, otherCullRecord.envelope, 0)) {
                            return false;
                        }
                    }
                }
            }
        }
        return true;
    }

    bool LabelCuller::calculateScreenEnvelope(const std::shared_ptr<Label>& label, std::array<cglib::vec2<float>, 4>& envelope) const {
        std::array<cglib::vec3<float>, 4> mapEnvelope;
        if (!label->calculateEnvelope((label->getStyle()->sizeFunc)(_viewState), EXTRA_LABEL_BUFFER, _viewState, mapEnvelope)) {
            return false;
        }
        
        for (int i = 0; i < 4; i++) {
            cglib::vec2<float> p_proj(cglib::proj_o(cglib::transform_point(mapEnvelope[i], _localCameraProjMatrix)));
            envelope[i] = p_proj;
        }
        return true;
    }
} }
