#include "LabelCuller.h"
#include "LabelSlice.h"
#include "RenderStats.h"

#include <array>
#include <cmath>
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
        std::size_t i0 = N1 - 1;
        for (std::size_t i1 = 0; i1 < N1; ++i1) {
            cglib::vec2<float> edge = vertList1[i1] - vertList1[i0];
            if (edge == cglib::vec2<float>::zero()) {
                continue;
            }
            cglib::vec2<float> proj(edge(1), -edge(0));

            float min1, max1, min2, max2;
            gatherPolygonProjectionExtents(vertList1, proj, min1, max1);
            gatherPolygonProjectionExtents(vertList2, proj, min2, max2);
            // The buffer widens the whole axis, not this edge; 'proj' is not normalized, so scale the gap like the extents.
            float gap = buffer * cglib::length(proj);
            if (max1 + gap < min2 || min1 - gap > max2) {
                return true;
            }

            i0 = i1;
        }
        return false;
    }

    template <std::size_t N1, std::size_t N2>
    static bool testPolygonOverlap(const std::array<cglib::vec2<float>, N1>& vertList1, const std::array<cglib::vec2<float>, N2>& vertList2, float buffer) {
        return !findSeparatingAxis(vertList1, vertList2, buffer) && !findSeparatingAxis(vertList2, vertList1, buffer);
    }
}

namespace {
    // Holds a mutex across a loop, handing it back every 'batch' iterations: the GL thread needs the
    // label mutex to build vertices, and locking once per label costs more in contention than work.
    class BatchLock final {
    public:
        BatchLock(std::mutex& mutex, int batch) : _lock(mutex), _batch(batch) { }

        void step() {
            if (++_count >= _batch) {
                _count = 0;
                _lock.unlock();
                _lock.lock();
            }
        }

        void release() { _lock.unlock(); }
        void acquire() { _count = 0; _lock.lock(); }

    private:
        std::unique_lock<std::mutex> _lock;
        const int _batch;
        int _count = 0;
    };
}

namespace massif::vt {
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

    void LabelCuller::setMetersToInternal(double metersToInternal) {
        std::lock_guard<std::mutex> lock(_mutex);

        _metersToInternal = metersToInternal;
    }

    void LabelCuller::setLabelViewDistance(double viewDistance) {
        std::lock_guard<std::mutex> lock(_mutex);

        _labelViewDistance = viewDistance;
    }

    void LabelCuller::setOcclusionTest(std::function<bool(const cglib::vec3<double>&)> test) {
        std::lock_guard<std::mutex> lock(_mutex);

        _occlusionTest = std::move(test);
    }

    void LabelCuller::beginSlice(double budgetMs) {
        std::lock_guard<std::mutex> lock(_mutex);

        _sliceBudgeted = budgetMs > 0;
        _sliceExhausted = false;
        _sliceDeadline = std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double, std::milli>(budgetMs));
    }

    bool LabelCuller::isSliceExhausted() const {
        std::lock_guard<std::mutex> lock(_mutex);

        return _sliceExhausted;
    }

    void LabelCuller::reset() {
        std::lock_guard<std::mutex> lock(_mutex);

        clearGrid();
    }

    bool LabelCuller::process(const std::vector<std::shared_ptr<Label>>& labelList, std::mutex& labelMutex, std::size_t& cursor) {
        std::lock_guard<std::mutex> lock(_mutex);

        if (cursor > labelList.size()) {
            cursor = 0; // the label set was rebuilt under us; start this layer again
        }

        VT_STAT_INC(cullerPasses);
        VT_STAT_CLOCK(cullerClock);

        // The grid is not cleared: one culler serves every layer of a pass, so labels of different layers collide.

        std::vector<LabelInfo> validLabelList;
        validLabelList.reserve(labelList.size());
        // Only labelDistance (view::distance) changes per label, so the ViewState copy stays out of the loop.
        ViewState rankViewState = _viewState;
        // Reused across labels to avoid two heap allocations per label per pass.
        std::vector<std::array<cglib::vec3<float>, 4>> worldEnvelopes;
        std::vector<std::vector<std::array<cglib::vec3<float>, 4>>> partEnvelopes;
        CullRecord partRecord;
        std::vector<CullRecord> variants;
        VT_STAT_CLOCK(phaseClock);
        BatchLock labelLock(labelMutex, LABEL_LOCK_BATCH);
        // Slicing the collect (most of a pass, performance-log 28) runs before the sort, so it costs no ordering.
        const std::size_t sliceStart = cursor;
        std::size_t index = cursor;
        for (; index < labelList.size(); index++) {
            if (_sliceBudgeted && labelSliceMayStop(index, sliceStart, MIN_SLICE_LABELS) && std::chrono::steady_clock::now() > _sliceDeadline) {
                _sliceExhausted = true;
                break;
            }
            const std::shared_ptr<Label>& label = labelList[index];
            labelLock.step();

            if (!label->isActive()) {
                continue;
            }
            VT_STAT_INC(cullerConsidered);

            // Before updatePlacement, which may reset opacity to 0 even for a label already on screen.
            bool wasVisible = label->isVisible();

            // Glyphs are screen-space, so distant features would fill the horizon with full-size labels.
            // Hidden rather than skipped, so the GL thread's opacity animation fades them.
            const std::shared_ptr<const TileLabel::Style>& style = label->getStyle();
            bool ranked = !(style->rankFunc == FloatFunction(0.0f));
            float maxDistance = style->maxDistance;
            float distance = 0; // meters, 0 when it could not be resolved
            double cameraToCenter = _viewState.focusDistance;
            bool measurable = (maxDistance > 0 || ranked || cameraToCenter > 0) && (_metersToInternal > 0 || cameraToCenter > 0);
            bool measured = false;
            // Returns false when the distance alone hides the label.
            auto measureDistance = [&]() {
                cglib::vec3<double> position(0, 0, 0);
                if (!label->calculateCenter(position)) {
                    return true;
                }
                measured = true;
                double internalDistance = cglib::length(position - _viewState.origin);
                if (LabelDistance::isTooFar(cameraToCenter, internalDistance, _labelViewDistance)) {
                    VT_STAT_INC(cullerDistanceCut);
                    label->setVisible(false);
                    return false;
                }
                if (_metersToInternal > 0) {
                    distance = static_cast<float>(internalDistance / _metersToInternal);
                    if (maxDistance > 0 && distance > maxDistance) {
                        VT_STAT_INC(cullerMaxDistanceCut);
                        label->setVisible(false);
                        return false;
                    }
                }
                return true;
            };
            // The perspective cut first: one length, before any per-label work (performance-log 27).
            if (measurable && !measureDistance()) {
                continue;
            }

            if (label->updatePlacement(_viewState)) {
                label->setOpacity(0);
            }
            // A first-time label has no centre until updatePlacement; at distance 0 it would outrank nearer ones.
            if (measurable && !measured && !measureDistance()) {
                continue;
            }

            if (!label->isValid()) {
                VT_STAT_INC(cullerInvalid);
            }
            // Hidden by the terrain: take no collision slot (see setOcclusionTest). After updatePlacement
            // because most considered labels are off-screen and the test is not cheap.
            if (label->isValid() && _occlusionTest) {
                cglib::vec3<double> anchor(0, 0, 0);
                if (label->calculateCenter(anchor) && _occlusionTest(anchor)) {
                    VT_STAT_INC(cullerOccluded);
                    label->setVisible(false);
                    continue;
                }
            }
            if (label->isValid()) {
                float size = (style->sizeFunc)(_viewState);
                // The one per-label style evaluation, so the only place view::distance means anything;
                // it only reorders the greedy insertion below.
                float priority = label->getPriority();
                if (ranked) {
                    rankViewState.labelDistance = distance;
                    priority += (style->rankFunc)(rankViewState);
                }
                bool valid = label->calculateVariantEnvelopes(size, EXTRA_LABEL_BUFFER, _viewState, worldEnvelopes, partEnvelopes);
                variants.clear();
                for (std::size_t i = 0; i < worldEnvelopes.size(); i++) {
                    CullRecord& record = variants.emplace_back();
                    projectEnvelope(worldEnvelopes[i], record);
                    for (const std::array<cglib::vec3<float>, 4>& part : partEnvelopes[i]) {
                        projectEnvelope(part, partRecord);
                        record.parts.push_back(partRecord.envelope);
                        record.partBounds.push_back(partRecord.bounds);
                        record.axisAligned = record.axisAligned && partRecord.axisAligned;
                    }
                    // Snapshot the identity fields; the placement (and thus the local id) can be
                    // changed concurrently by tile updates once labelMutex is released.
                    record.localId = label->getLocalId();
                    record.allowOverlapSameFeatureId = label->allowOverlapSameFeatureId();
                }
                int variantIndex = std::min(static_cast<int>(variants.size()) - 1, std::max(0, label->getVariantIndex()));
                validLabelList.push_back({ valid, wasVisible, label->isPartlyOccluded(), priority, label->getLayerIndex(), size, label->getOpacity(), label, variants[variantIndex],
                                           variants.size() > 1 ? variants : std::vector<CullRecord>() });
            }
        }

        cursor = index;
        VT_STAT_ADD(cullerSorted, static_cast<long long>(validLabelList.size()));

        // Released around the sort: long enough for the GL thread to notice, and it reads no state that thread writes.
        labelLock.release();
        VT_STAT_SPLIT(cullerCollectNs, phaseClock);

        // Previously visible labels go before new ones of equal priority (MapLibre's committed placement);
        // wasVisible, not opacity, since updatePlacement() can reset the opacity of a visible label.
        std::stable_sort(validLabelList.begin(), validLabelList.end(), [&](const LabelInfo& labelInfo1, const LabelInfo& labelInfo2) {
            // A label a building half hides gives way to any label in the clear, whatever their priorities.
            if (labelInfo1.occluded != labelInfo2.occluded) {
                return labelInfo2.occluded;
            }
            if (labelInfo1.priority != labelInfo2.priority) {
                return labelInfo1.priority > labelInfo2.priority;
            }
            if (labelInfo1.wasVisible != labelInfo2.wasVisible) {
                return labelInfo1.wasVisible;
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

        VT_STAT_SPLIT(cullerSortNs, phaseClock);

        std::unordered_map<long long, std::vector<const LabelInfo*>> groupMap;
        groupMap.reserve(validLabelList.size());
        bool changed = false;
        // Labels of one group must stay the group's minimum distance apart. A callout tests it at every row
        // it tries; testing after placement would take a free row and then hide the label anyway.
        auto testGroupDistance = [&groupMap](const LabelInfo& info) {
            long long groupId = info.label->getGroupId();
            if (groupId <= 0) {
                return true;
            }
            for (const LabelInfo* otherLabelInfo : groupMap[groupId]) {
                float minimumDistance = std::min(info.label->getMinimumGroupDistance(), otherLabelInfo->label->getMinimumGroupDistance());
                if ((!info.cullRecord.allowOverlapSameFeatureId || !otherLabelInfo->cullRecord.allowOverlapSameFeatureId || info.cullRecord.localId != otherLabelInfo->cullRecord.localId) && testRecordOverlap(info.cullRecord, otherLabelInfo->cullRecord, minimumDistance)) {
                    return false;
                }
            }
            return true;
        };

        // A following band sits just above the highest summit it names (peakfinder's row), capped by the screen anchor.
        _highestCalloutAnchorY = -1.0f;
        for (const LabelInfo& labelInfo : validLabelList) {
            const std::shared_ptr<const TileLabel::Style>& style = labelInfo.label->getStyle();
            if (labelInfo.valid && style->orientation == LabelOrientation::CALLOUT && style->calloutScreenAnchor >= 0 && style->calloutBandFollow) {
                float anchorY = labelInfo.label->calculateAnchorScreenY(_viewState);
                cglib::vec3<double> anchorPosition(0, 0, 0);
                if (anchorY > 0 && anchorY < _viewState.resolution && anchorY > _highestCalloutAnchorY && labelInfo.label->calculateCenter(anchorPosition)) {
                    _highestCalloutAnchorY = anchorY;
                    _highestCalloutAnchorPosition = anchorPosition;
                }
            }
        }

        for (int y = 0; y < GRID_RESOLUTION_Y; y++) {
            for (int x = 0; x < GRID_RESOLUTION_X; x++) {
                _reservedGrid[y][x].clear();
            }
        }
        for (std::size_t i = 0; i < validLabelList.size(); i++) {
            const LabelInfo& labelInfo = validLabelList[i];
            if (labelInfo.valid && labelInfo.wasVisible && !labelInfo.occluded && labelInfo.label->getGroupId() >= 0) {
                CullRecord record = labelInfo.cullRecord;
                record.reservation = static_cast<int>(i);
                addGridRecord(_reservedGrid, record);
            }
        }

        labelLock.acquire();
        for (std::size_t labelIndex = 0; labelIndex < validLabelList.size(); labelIndex++) {
            LabelInfo& labelInfo = validLabelList[labelIndex];
            labelLock.step();

            const std::shared_ptr<Label>& label = labelInfo.label;

            long long groupId = label->getGroupId();

            // A negative group id means always visible.
            bool visible;
            if (label->getStyle()->orientation == LabelOrientation::CALLOUT && groupId >= 0) {
                visible = labelInfo.valid && placeCalloutLabel(labelInfo, testGroupDistance);
            } else if (labelInfo.variants.size() > 1 && groupId >= 0) {
                visible = labelInfo.valid && placeAnchoredLabel(labelInfo, static_cast<int>(labelIndex), testGroupDistance);
            } else {
                visible = groupId >= 0 ? labelInfo.valid && testGridOverlap(labelInfo) : labelInfo.valid;
                visible = visible && testGroupDistance(labelInfo);
            }

            if (!labelInfo.valid) {
                VT_STAT_INC(cullerNotFacing);
            } else if (!visible) {
                VT_STAT_INC(cullerCollided);
            }

            if (visible) {
                VT_STAT_INC(cullerVisible);
                // A road name behind a building is drawn at opacity 0, and it hid the POI on the roof in front.
                if (groupId >= 0 && !label->isFullyOccluded()) {
                    addGridRecord(_recordGrid, labelInfo.cullRecord);
                }
                if (groupId > 0) {
                    groupMap[groupId].push_back(&labelInfo);
                }
            }
            if (visible && isOffscreen(labelInfo.cullRecord)) {
                label->setOpacity(1.0f); // see isOffscreen - it arrives already drawn
                if (label->drawsText()) {
                    label->setTextOpacity(1.0f);
                }
            }
            if (visible != label->isVisible()) {
                label->setVisible(visible);
                VT_STAT_INC(cullerVisibilityFlips);
                changed = true;
            }
        }
        VT_STAT_SPLIT(cullerInsertNs, phaseClock);
        VT_STAT_SPLIT(cullerNs, cullerClock);
        return changed;
    }

    // Spans the padded viewport: labels are placed up to labelPadding outside the screen, and a grid
    // clamped at the screen edge would pile them into the border cells.
    cglib::vec2<int> LabelCuller::getGridIndex(const cglib::vec2<float>& pos) const {
        float padding = _viewState.labelPadding;
        float width = _viewState.resolution * _viewState.aspect + 2 * padding;
        float height = _viewState.resolution + 2 * padding;
        int x = std::max(0, std::min(GRID_RESOLUTION_X - 1, static_cast<int>(GRID_RESOLUTION_X * (pos(0) + padding) / width)));
        int y = std::max(0, std::min(GRID_RESOLUTION_Y - 1, static_cast<int>(GRID_RESOLUTION_Y * (pos(1) + padding) / height)));
        return cglib::vec2<int>(x, y);
    }

    // Outside the real viewport but inside the padded one: maplibre's JointPlacement::skipFade, so a label
    // panned into view is already drawn instead of still fading in mid-screen.
    bool LabelCuller::isOffscreen(const CullRecord& cullRecord) const {
        float width = _viewState.resolution * _viewState.aspect, height = _viewState.resolution;
        return cullRecord.bounds.max(0) < 0 || cullRecord.bounds.min(0) > width ||
               cullRecord.bounds.max(1) < 0 || cullRecord.bounds.min(1) > height;
    }

    void LabelCuller::clearGrid() {
        for (int y = 0; y < GRID_RESOLUTION_Y; y++) {
            for (int x = 0; x < GRID_RESOLUTION_X; x++) {
                _recordGrid[y][x].clear();
            }
        }
    }

    void LabelCuller::takeVariant(LabelInfo& labelInfo, int index) {
        labelInfo.label->setVariantIndex(index);
        labelInfo.cullRecord = labelInfo.variants[index];
    }

    void LabelCuller::addGridRecord(RecordGrid& grid, const CullRecord& cullRecord) const {
        cglib::vec2<int> minPos = getGridIndex(cullRecord.bounds.min);
        cglib::vec2<int> maxPos = getGridIndex(cullRecord.bounds.max);
        for (int y = minPos(1); y <= maxPos(1); y++) {
            for (int x = minPos(0); x <= maxPos(0); x++) {
                grid[y][x].push_back(cullRecord);
            }
        }
    }

    bool LabelCuller::testRecordOverlap(const CullRecord& record1, const CullRecord& record2, float buffer) {
        // Callers pass records whose bounds intersect; for two axis-aligned boxes that already is an overlap.
        if (record1.parts.empty() && record2.parts.empty()) {
            if (buffer <= 0 && record1.axisAligned && record2.axisAligned) {
                return true;
            }
            return testPolygonOverlap(record1.envelope, record2.envelope, buffer);
        }
        std::size_t count1 = std::max<std::size_t>(1, record1.parts.size()), count2 = std::max<std::size_t>(1, record2.parts.size());
        for (std::size_t part1 = 0; part1 < count1; part1++) {
            const cglib::bbox2<float>& bounds1 = (record1.parts.empty() ? record1.bounds : record1.partBounds[part1]);
            if (buffer <= 0 && !bounds1.inside(record2.bounds)) {
                continue;
            }
            for (std::size_t part2 = 0; part2 < count2; part2++) {
                const cglib::bbox2<float>& bounds2 = (record2.parts.empty() ? record2.bounds : record2.partBounds[part2]);
                if (buffer <= 0 && !bounds1.inside(bounds2)) {
                    continue;
                }
                if ((buffer <= 0 && record1.axisAligned && record2.axisAligned) || testPolygonOverlap(record1.parts.empty() ? record1.envelope : record1.parts[part1], record2.parts.empty() ? record2.envelope : record2.parts[part2], buffer)) {
                    return true;
                }
            }
        }
        return false;
    }

    bool LabelCuller::testGridOverlap(const LabelInfo& labelInfo) const {
        const CullRecord& cullRecord = labelInfo.cullRecord;
        cglib::vec2<int> minPos = getGridIndex(cullRecord.bounds.min);
        cglib::vec2<int> maxPos = getGridIndex(cullRecord.bounds.max);
        bool hasFoundTheSame = false;
        for (int y = minPos(1); y <= maxPos(1); y++) {
            for (int x = minPos(0); x <= maxPos(0); x++) {
                for (const CullRecord& otherRecord : _recordGrid[y][x]) {
                    if (otherRecord.bounds.inside(cullRecord.bounds)) {
                        if ((!cullRecord.allowOverlapSameFeatureId || !otherRecord.allowOverlapSameFeatureId || cullRecord.localId != otherRecord.localId) && testRecordOverlap(otherRecord, cullRecord, 0)) {
                            return false;
                        }
                    }
                    if (cullRecord.localId == otherRecord.localId) {
                        hasFoundTheSame = true;
                    }
                }
            }
        }
        return (!labelInfo.label->sameFeatureIdDependent() || hasFoundTheSame);
    }

    bool LabelCuller::placeCalloutLabel(LabelInfo& labelInfo, const std::function<bool(const LabelInfo&)>& testGroupDistance) {
        const std::shared_ptr<Label>& label = labelInfo.label;
        const std::shared_ptr<const TileLabel::Style>& style = label->getStyle();

        bool banded = style->calloutScreenAnchor >= 0;
        bool following = banded && style->calloutBandFollow && _highestCalloutAnchorY >= 0;
        auto envelopeAt = [this, &labelInfo, &label, banded, following](float offset) {
            if (!banded) {
                label->setCalloutOffset(offset);
            } else if (following) {
                label->setCalloutPlacement(offset, label->calculateAnchorScreenY(_viewState), _highestCalloutAnchorPosition, _highestCalloutAnchorY);
            } else {
                label->setCalloutPlacement(offset, label->calculateAnchorScreenY(_viewState));
            }
            labelInfo.valid = calculateScreenEnvelope(label, labelInfo.size, labelInfo.cullRecord);
            return labelInfo.valid;
        };

        // The point the band line runs through: the box bottom by default; tilted names read as a row
        // only when all hang from the same corner, which the style anchor picks.
        auto bandAnchorY = [&labelInfo, &style]() {
            if (!style->calloutBandAnchor) {
                return labelInfo.cullRecord.bounds.min(1);
            }
            const std::array<cglib::vec2<float>, 4>& e = labelInfo.cullRecord.envelope;
            float u = ((*style->calloutBandAnchor)(0) + 1.0f) * 0.5f, v = ((*style->calloutBandAnchor)(1) + 1.0f) * 0.5f;
            float bottom = e[0](1) + (e[1](1) - e[0](1)) * u;
            float top = e[3](1) + (e[2](1) - e[3](1)) * u;
            return bottom + (top - bottom) * v;
        };

        // Every pass re-places a callout from scratch; keeping its old row stops names re-flowing while panning.
        float previousOffset = label->getCalloutOffset();

        // Either a band at a fixed screen height (one row of names over the ridges) or above its own anchor.
        if (!envelopeAt(0)) {
            return false;
        }
        // Looking up: a name whose feature is under the bottom edge has nothing on screen to point at.
        if (style->calloutAnchorVisible && labelInfo.cullRecord.bounds.min(1) < 0) {
            label->setCalloutFailures(0);
            return false;
        }
        float anchorY = bandAnchorY();
        float top = labelInfo.cullRecord.bounds.max(1);

        // Screen pixels from here on; the draw path converts the lift at the label's depth, so it holds
        // under any camera move. The style's offset and step are device pixels, calloutPixel normalizes them.
        float calloutPixel = std::max(1.0f, _viewState.resolution * style->scale * 0.5f);
        if (_viewState.deviceResolution > 0 && _viewState.resolution > 0) {
            calloutPixel *= _viewState.resolution / _viewState.deviceResolution;
        }
        float calloutOffset = style->calloutOffset * calloutPixel;
        float lift = calloutOffset;
        if (banded) {
            float bandY = (1.0f - style->calloutScreenAnchor) * _viewState.resolution;
            if (following) {
                bandY = std::min(bandY, _highestCalloutAnchorY + calloutOffset + calloutPixel);
            }
            float bandLift = bandY - anchorY;
            // The band is the height, not a floor: a name that cannot reach it clear of its feature is dropped.
            if (bandLift < calloutOffset) {
                label->setCalloutFailures(0);
                return false;
            }
            lift = bandLift;
        }
        // A lifted label must stay on screen; its anchor being in view proves nothing. A constant margin,
        // so tall names are not pushed down further than short ones.
        float maxLift = _viewState.resolution - top - SCREEN_EDGE_MARGIN;
        // Rows may step down, but never below the style's lift: the leader line only exists above the feature.
        float minLift = std::max(calloutOffset, SCREEN_EDGE_MARGIN - labelInfo.cullRecord.bounds.min(1));
        if (minLift > maxLift) {
            label->setCalloutFailures(0);
            return false;
        }
        // A band or nothing: clamping to maxLift, built from the label's own top, would offset each name by its height.
        if (banded) {
            if (lift > maxLift) {
                label->setCalloutFailures(0);
                return false;
            }
            lift = std::max(lift, minLift);
        } else {
            lift = std::max(std::min(lift, maxLift), minLift);
        }

        // Not '> 0': a negative step stacks rows downwards, the only room a band near the top has.
        float step = (style->calloutStep != 0.0f ? style->calloutStep * calloutPixel : labelInfo.size * 1.2f * calloutPixel);

        // The held row first, if this pass still offers it; changing rows while the camera moves reads as flicker.
        if (labelInfo.wasVisible && previousOffset > 0) {
            bool holdsOfferedRow = previousOffset >= minLift - 0.5f && previousOffset <= maxLift + 0.5f;
            // A band's held offset must be one of this pass's rows: the band's lift moves with tilt.
            if (holdsOfferedRow && banded) {
                holdsOfferedRow = false;
                for (int row = 0; row < std::max(1, style->calloutMaxRows); row++) {
                    if (std::abs(previousOffset - (lift + row * step)) < 0.5f) {
                        holdsOfferedRow = true;
                        break;
                    }
                }
            }
            if (holdsOfferedRow) {
                if (envelopeAt(previousOffset) && testGridOverlap(labelInfo) && testGroupDistance(labelInfo)) {
                    label->setCalloutFailures(0);
                    return true;
                }
            }
        }

        for (int row = 0; row < std::max(1, style->calloutMaxRows); row++) {
            float rowLift = lift + row * step;
            if (rowLift > maxLift || rowLift < minLift) {
                break; // rows are monotonic, nothing further fits
            }
            if (!envelopeAt(rowLift)) {
                return false;
            }
            if (testGridOverlap(labelInfo) && testGroupDistance(labelInfo)) {
                label->setCalloutFailures(0);
                return true;
            }
        }

        // A name on screen may persist a few passes rather than blink as tiles stream in: on the band's line,
        // ignoring the group distance but never overlapping, since a granted overlap would stick.
        if (labelInfo.wasVisible && label->getCalloutFailures() < style->calloutPersistPasses) {
            if (envelopeAt(lift) && testGridOverlap(labelInfo)) {
                label->setCalloutFailures(label->getCalloutFailures() + 1);
                return true;
            }
        }
        label->setCalloutFailures(0);
        return false;
    }

    bool LabelCuller::testReservedOverlap(const CullRecord& cullRecord, int index) const {
        cglib::vec2<int> minPos = getGridIndex(cullRecord.bounds.min);
        cglib::vec2<int> maxPos = getGridIndex(cullRecord.bounds.max);
        for (int y = minPos(1); y <= maxPos(1); y++) {
            for (int x = minPos(0); x <= maxPos(0); x++) {
                for (const CullRecord& otherRecord : _reservedGrid[y][x]) {
                    if (otherRecord.reservation > index && otherRecord.bounds.inside(cullRecord.bounds) &&
                        (!cullRecord.allowOverlapSameFeatureId || !otherRecord.allowOverlapSameFeatureId || cullRecord.localId != otherRecord.localId) &&
                        testRecordOverlap(otherRecord, cullRecord, 0)) {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    bool LabelCuller::placeAnchoredLabel(LabelInfo& labelInfo, int labelIndex, const std::function<bool(const LabelInfo&)>& testGroupDistance) {
        const std::shared_ptr<Label>& label = labelInfo.label;
        int count = static_cast<int>(labelInfo.variants.size());

        // The held side first, against flicker; never the icon-only variant, which always fits and would stick.
        std::vector<int> candidates;
        candidates.reserve(count + 1);
        int preferred = (label->drawsText() ? label->getVariantIndex() : -1);
        if (preferred >= 0 && preferred < count) {
            candidates.push_back(preferred);
        }
        for (int index = 0; index < count; index++) {
            if (index != preferred) {
                candidates.push_back(index);
            }
        }

        int evicting = -1;
        for (int index : candidates) {
            takeVariant(labelInfo, index);
            if (testGridOverlap(labelInfo) && testGroupDistance(labelInfo)) {
                if (!label->drawsText() || !testReservedOverlap(labelInfo.cullRecord, labelIndex)) {
                    return true;
                }
                if (evicting < 0) {
                    evicting = index;
                }
            }
        }
        if (evicting >= 0) {
            takeVariant(labelInfo, evicting);
            return true;
        }

        // Nothing free: fade out on the side it held, not the last one tried.
        takeVariant(labelInfo, std::max(0, std::min(count - 1, label->getVariantIndex())));
        return false;
    }

    void LabelCuller::projectEnvelope(const std::array<cglib::vec3<float>, 4>& worldEnvelope, CullRecord& record) const {
        std::array<cglib::vec2<float>, 4>& envelope = record.envelope;
        for (std::size_t i = 0; i < 4; i++) {
            cglib::vec2<float> p = cglib::proj_o(cglib::transform_point(worldEnvelope[i], _localCameraProjMatrix));
            envelope[i] = cglib::vec2<float>((p(0) * 0.5f + 0.5f) * _viewState.resolution * _viewState.aspect, (p(1) * 0.5f + 0.5f) * _viewState.resolution);
        }
        record.bounds = cglib::bbox2<float>::make_union(envelope.begin(), envelope.end());
        record.axisAligned = std::abs(envelope[0](1) - envelope[1](1)) < AXIS_ALIGNED_EPSILON &&
                             std::abs(envelope[1](0) - envelope[2](0)) < AXIS_ALIGNED_EPSILON &&
                             std::abs(envelope[2](1) - envelope[3](1)) < AXIS_ALIGNED_EPSILON &&
                             std::abs(envelope[3](0) - envelope[0](0)) < AXIS_ALIGNED_EPSILON;
    }

    bool LabelCuller::calculateScreenEnvelope(const std::shared_ptr<Label>& label, float size, CullRecord& record) const {
        std::array<cglib::vec3<float>, 4> worldEnvelope;
        if (!label->calculateEnvelope(size, EXTRA_LABEL_BUFFER, _viewState, worldEnvelope)) {
            return false;
        }

        projectEnvelope(worldEnvelope, record);
        return true;
    }
}
