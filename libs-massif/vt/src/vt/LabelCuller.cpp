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
            // The buffer widens the AXIS, not this one edge: measured against the edge, the OPPOSITE
            // edge of a rectangle reported a separating axis before the near one was tested. 'proj' is
            // not normalized, so the gap is scaled the way the extents are.
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
    // Holds a mutex across a loop, handing it back every 'batch' iterations. The GL thread holds
    // the label mutex while it builds label vertices, so a placement pass may not keep it for its
    // whole run - but taking it once per label spent more time contending for it than working.
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

        // The grid is intentionally NOT cleared here: one culler is shared by every layer in a placement
        // pass, so records must accumulate across process() calls for labels of different layers to
        // collide. Each pass builds a fresh culler, so no stale records survive.

        // Start by collecting valid labels and updating label placements
        std::vector<LabelInfo> validLabelList;
        validLabelList.reserve(labelList.size());
        // The view a ranking expression is evaluated against: the frame's, plus the distance to
        // the label being ranked (style variable view::distance). Kept out of the loop - a
        // ViewState carries the matrices and the frustum, and only its distance changes here.
        ViewState rankViewState = _viewState;
        // Reused across labels: a pass walks a couple of thousand of them, and both buffers would
        // otherwise be a heap allocation each, per label, per pass.
        std::vector<std::array<cglib::vec3<float>, 4>> worldEnvelopes;
        std::vector<CullRecord> variants;
        VT_STAT_CLOCK(phaseClock);
        BatchLock labelLock(labelMutex, LABEL_LOCK_BATCH);
        // Collect is ~90% of a pass (performance-log 28), and it runs BEFORE the sort, so cutting
        // it short costs no ordering among the labels that do get collected.
        const std::size_t sliceStart = cursor;
        std::size_t index = cursor;
        for (; index < labelList.size(); index++) {
            if (_sliceBudgeted && labelSliceMayStop(index, sliceStart, MIN_SLICE_LABELS) && std::chrono::steady_clock::now() > _sliceDeadline) {
                _sliceExhausted = true;
                break;
            }
            const std::shared_ptr<Label>& label = labelList[index];
            labelLock.step();

            // Analyze only active and valid labels
            if (!label->isActive()) {
                continue;
            }
            VT_STAT_INC(cullerConsidered);

            // Capture visibility from the previous frame BEFORE updatePlacement, which may
            // reset opacity to 0 even for labels that were already visible on screen.
            bool wasVisible = label->isVisible();

            // Style max-distance: a label glyph is screen-space, so an unlimited view fills its horizon
            // band with full-size labels for features kilometres away. HIDDEN rather than skipped, so
            // the GL thread's existing opacity animation fades it out and back in.
            const std::shared_ptr<const TileLabel::Style>& style = label->getStyle();
            bool ranked = !(style->rankFunc == FloatFunction(0.0f));
            float maxDistance = style->maxDistance;
            float distance = 0; // meters, 0 when it could not be resolved
            double cameraToCenter = _viewState.focusDistance;
            if ((maxDistance > 0 || ranked || cameraToCenter > 0) && (_metersToInternal > 0 || cameraToCenter > 0)) {
                cglib::vec3<double> position(0, 0, 0);
                if (label->calculateCenter(position)) {
                    double internalDistance = cglib::length(position - _viewState.origin);
                    // The perspective cut comes FIRST and costs one length: everything below it -
                    // updatePlacement, the variant envelopes, the grid test - is per label, and the
                    // horizon band is where most of the labels are (performance-log 27).
                    if (LabelDistance::isTooFar(cameraToCenter, internalDistance, _labelViewDistance)) {
                        VT_STAT_INC(cullerDistanceCut);
                        label->setVisible(false);
                        continue;
                    }
                    if (_metersToInternal > 0) {
                        distance = static_cast<float>(internalDistance / _metersToInternal);
                        if (maxDistance > 0 && distance > maxDistance) {
                            VT_STAT_INC(cullerMaxDistanceCut);
                            label->setVisible(false);
                            continue;
                        }
                    }
                }
            }

            if (label->updatePlacement(_viewState)) {
                label->setOpacity(0);
            }

            if (!label->isValid()) {
                VT_STAT_INC(cullerInvalid);
            }
            // Hidden by the terrain: drop it before it can take a collision slot a visible neighbour
            // needs. See setOcclusionTest.
            // AFTER updatePlacement on purpose: the test is a matrix transform and five texture taps,
            // and most CONSIDERED labels are off-screen (cullerInvalid). Asking before the placement
            // ran it on every one of them and cost ~18 ms a pass against ~4.
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
                // Ranking is the label's own priority plus what the style makes of the view - the one
                // per-label evaluation, so the one place view::distance means anything. It only reorders
                // the greedy insertion below; size and colour still come from the batch.
                float priority = label->getPriority();
                if (ranked) {
                    rankViewState.labelDistance = distance;
                    priority += (style->rankFunc)(rankViewState);
                }
                // Every side in one call - they share the placement and the label's screen axes,
                // so this is one placement per label however many sides it has.
                bool valid = label->calculateVariantEnvelopes(size, EXTRA_LABEL_BUFFER, _viewState, worldEnvelopes);
                variants.clear();
                for (const std::array<cglib::vec3<float>, 4>& worldEnvelope : worldEnvelopes) {
                    CullRecord& record = variants.emplace_back();
                    projectEnvelope(worldEnvelope, record);
                    // Snapshot the identity fields; the placement (and thus the local id) can be
                    // changed concurrently by tile updates once labelMutex is released.
                    record.localId = label->getLocalId();
                    record.allowOverlapSameFeatureId = label->allowOverlapSameFeatureId();
                }
                int variantIndex = std::min(static_cast<int>(variants.size()) - 1, std::max(0, label->getVariantIndex()));
                // Only a label with several sides needs them kept - one layout is fully described
                // by its cull record.
                validLabelList.push_back({ valid, wasVisible, priority, label->getLayerIndex(), size, label->getOpacity(), label, variants[variantIndex],
                                           variants.size() > 1 ? variants : std::vector<CullRecord>() });
            }
        }

        cursor = index;
        VT_STAT_ADD(cullerSorted, static_cast<long long>(validLabelList.size()));

        // Handed back around the sort: it is the one stretch of a pass long enough for the GL
        // thread to notice, and it reads no label state that thread writes.
        labelLock.release();
        VT_STAT_SPLIT(cullerCollectNs, phaseClock);

        // Sort by priority/wasVisible/layerIndex/size/opacity: a label visible in the previous frame is
        // placed before a new one of equal priority, which is MapLibre's "committed placement". The
        // isVisible() boolean beats opacity, which updatePlacement() can reset even for visible labels.
        std::stable_sort(validLabelList.begin(), validLabelList.end(), [&](const LabelInfo& labelInfo1, const LabelInfo& labelInfo2) {
            if (labelInfo1.priority != labelInfo2.priority) {
                return labelInfo1.priority > labelInfo2.priority;
            }
            if (labelInfo1.wasVisible != labelInfo2.wasVisible) {
                return labelInfo1.wasVisible; // previously-visible labels claim grid slots first
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

        // Update label visibility flag based on overlap analysis
        std::unordered_map<long long, std::vector<const LabelInfo*>> groupMap;
        groupMap.reserve(validLabelList.size());
        bool changed = false;
        // The group's minimum distance: labels of one group must not only miss each other but stay that
        // many pixels apart. A callout is tested for it AT EVERY ROW it tries - testing after placement
        // would put it on a free row and then hide it, which is what the stacking exists to avoid.
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

        // THE ROW FOLLOWS THE SKYLINE. A banded callout row sits just above the highest summit it
        // names, not at a fixed height: that is peakfinder.com's row, and at a fixed height every
        // summit standing above the line was dropped - which, with the skyline high on screen, was
        // all of them. The style's screen anchor is the highest the row may go.
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

        labelLock.acquire();
        for (LabelInfo& labelInfo : validLabelList) {
            labelLock.step();

            const std::shared_ptr<Label>& label = labelInfo.label;

            long long groupId = label->getGroupId();

            // Label is always visible if its group is set to negative value. Otherwise test visibility against other labels
            bool visible;
            if (label->getStyle()->orientation == LabelOrientation::CALLOUT && groupId >= 0) {
                visible = labelInfo.valid && placeCalloutLabel(labelInfo, testGroupDistance);
            } else if (labelInfo.variants.size() > 1 && groupId >= 0) {
                visible = labelInfo.valid && placeAnchoredLabel(labelInfo, testGroupDistance);
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
                if (groupId >= 0) {
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

    // The grid spans the PADDED viewport, not the screen: a label is now placed up to labelPadding
    // pixels outside it (ViewState), and a grid that stopped at the screen edge would clamp every
    // one of those into the border cells and let them collide with everything drawn there.
    cglib::vec2<int> LabelCuller::getGridIndex(const cglib::vec2<float>& pos) const {
        float padding = _viewState.labelPadding;
        float width = _viewState.resolution * _viewState.aspect + 2 * padding;
        float height = _viewState.resolution + 2 * padding;
        int x = std::max(0, std::min(GRID_RESOLUTION_X - 1, static_cast<int>(GRID_RESOLUTION_X * (pos(0) + padding) / width)));
        int y = std::max(0, std::min(GRID_RESOLUTION_Y - 1, static_cast<int>(GRID_RESOLUTION_Y * (pos(1) + padding) / height)));
        return cglib::vec2<int>(x, y);
    }

    // Entirely outside the REAL viewport, though inside the padded one it was placed against. This
    // is maplibre's JointPlacement::skipFade: "Because these symbols aren't onscreen yet, we can
    // skip the fade in animation, and if a subsequent viewport change brings them into view, they'll
    // be fully visible right away." Without it a label starts its fade at the edge and is still
    // fading when it has reached the middle of the screen.
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
        // Two screen-aligned rectangles whose bounds already intersect overlap, full stop - the
        // separating-axis test can only confirm it. This is the common case: labels are billboards.
        if (buffer <= 0 && record1.axisAligned && record2.axisAligned) {
            return true;
        }
        return testPolygonOverlap(record1.envelope, record2.envelope, buffer);
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

        // Until the next pass the name moves with its own anchor, or holds its band line - which,
        // for a band that follows the skyline, moves with the summit the band was put above.
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

        // Which point of the label the band line runs through. The default is the bottom of the
        // box, but a tilted panorama name reads as a row only when every label hangs from the SAME
        // corner - its first letter, or its last one - which is what the style anchor picks.
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

        // Where it ended up last time. A callout is re-placed from scratch on every pass, and a panning
        // map runs one whenever its tile set changes, so keeping the row it already holds is what stops
        // a screen of names re-flowing under the camera.
        float previousOffset = label->getCalloutOffset();

        // Where the label wants to sit before anything else is taken into account: either a band
        // at a fixed height on screen - which is what makes a panorama read as one row of names
        // over the ridges - or straight above its own anchor.
        if (!envelopeAt(0)) {
            return false;
        }
        float anchorY = bandAnchorY();
        float top = labelInfo.cullRecord.bounds.max(1);

        // Everything below is in SCREEN PIXELS, and so is the offset the label is given: it is converted
        // to world units at draw time against the projection at the label's own depth, so a lift of N
        // pixels stays N pixels while the camera tilts, rises or zooms.
        //
        // NORMALIZED screen pixels, though, while the style's own callout pixels - the offset and the
        // step - are DEVICE ones, like the glyph size they space out (Label::calculateLabelScale takes
        // the same). Hence the conversion: measured in this space they would otherwise be tighter or
        // looser than the names they separate by whatever the viewport's height happens to be, so a
        // row spacing that worked in portrait had the rows overlapping in landscape.
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
            // THE BAND IS THE HEIGHT, NOT A FLOOR. `std::max(lift, bandLift)` here meant that a
            // feature already ABOVE the band line got `calloutOffset` instead - its name placed just
            // over its own summit, which is the NO-BAND arrangement (omit the anchor for that). So a
            // banded style silently became a skyline one, label by label, for whichever summits
            // happened to sit high on screen: tilt down and rows of names left the band and appeared
            // under each other. Two placements from one style, switching as the camera moved.
            //
            // A name below its own summit is not wanted either, so there is nothing to fall back TO:
            // if the band cannot be reached while staying clear of the feature, the name is dropped.
            // The row loop and `minLift` below keep the same invariant for the stacked rows.
            if (bandLift < calloutOffset) {
                label->setCalloutFailures(0);
                return false;
            }
            lift = bandLift;
        }
        // Whatever the band asks for, the label has to stay on screen: lifted away from its anchor, its
        // own position is no evidence that it is in view. The margin is a CONSTANT - one proportional to
        // the label's height would push long names further down than short ones.
        float maxLift = _viewState.resolution - top - SCREEN_EDGE_MARGIN;
        // Rows may go down (negative step), but never below the lift the style asks for: the label
        // belongs ABOVE its feature, and its leader line only exists while it is.
        float minLift = std::max(calloutOffset, SCREEN_EDGE_MARGIN - labelInfo.cullRecord.bounds.min(1));
        // A feature already so high on screen that its name cannot fit above it AT ALL has no place
        // for that name: drop it. Pulling the label down to the screen edge instead put it BELOW its
        // own anchor, off the band the style asks for and with its leader line pointing down.
        if (minLift > maxLift) {
            label->setCalloutFailures(0);
            return false;
        }
        // A BANDED style gets the band or nothing. `maxLift` is built from `top`, the label's OWN
        // upper extent, so clamping down to it moves each name by its own height - and at a 55
        // degree orientation a plate is as tall as the name is long. That is why a band looked like
        // several: short names reached it, long ones stopped short, and tilting changed which. The
        // comment above aimed at a constant margin for exactly this reason, but `top` reintroduces
        // the dependence.
        //
        // The cost is the landscape case this clamp was added for: a band pinned 3% from the top is
        // 32 device pixels of a 1080-pixel short screen, less than a name and its plate, so every
        // label wants more room than there is and the whole row drops. That is now a style being
        // asked for something impossible rather than something to paper over - a top offset has to
        // leave room for a plate, and `peakFinderLabelBand` is the knob.
        if (banded) {
            if (lift > maxLift) {
                label->setCalloutFailures(0);
                return false;
            }
            lift = std::max(lift, minLift);
        } else {
            lift = std::max(std::min(lift, maxLift), minLift);
        }

        // NOT '> 0': a NEGATIVE step is how a style says its rows go DOWN, which is the only direction
        // a band pinned near the top of the screen has room in - and taking the default instead sent
        // them up into the edge margin, where `rowLift > maxLift` broke out of the row loop on the
        // first one. Every label past the first of a crowded band was therefore dropped rather than
        // stacked, which in a panorama is most of the horizon.
        float step = (style->calloutStep != 0.0f ? style->calloutStep * calloutPixel : labelInfo.size * 1.2f * calloutPixel);

        // The row it already holds is tried first, as long as it is still one this pass would
        // offer: a label that keeps changing row while the camera moves reads as flicker even
        // though it never disappears.
        if (labelInfo.wasVisible && previousOffset > 0) {
            // The rows this pass offers run from minLift to maxLift whichever way the step points:
            // comparing against `lift` alone refused every row BELOW the band, so a downward-stepping
            // style lost its held row on every pass and re-flowed the whole band.
            bool holdsOfferedRow = previousOffset >= minLift - 0.5f && previousOffset <= maxLift + 0.5f;
            // A BAND has NAMED rows, and that range is not them. Any offset the screen could hold
            // passed here, so a label kept whatever lift it was last placed at - and the lift the
            // band asks for MOVES as the camera tilts, because it is measured from the label's own
            // anchor. Tilting therefore left a row of names at last frame's heights and the band
            // looked broken; panning sideways "fixed" it only because the labels left the view, lost
            // `wasVisible`, and came back through the fresh placement below.
            //
            // So the held offset has to BE one of this pass's rows, or the label is re-placed.
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

        // Then row by row, in the direction the style's step points (DOWN for a negative one - a band
        // pinned to the top of the screen has no room above it), until the screen is free. Stepping
        // instead of hiding is the point: a summit that loses its slot still gets its name.
        for (int row = 0; row < std::max(1, style->calloutMaxRows); row++) {
            float rowLift = lift + row * step;
            if (rowLift > maxLift || rowLift < minLift) {
                break; // the rows all go the same way, so nothing beyond this one fits either
            }
            if (!envelopeAt(rowLift)) {
                return false;
            }
            if (testGridOverlap(labelInfo) && testGroupDistance(labelInfo)) {
                label->setCalloutFailures(0);
                return true;
            }
        }

        // Nothing free. A name already on screen may hold its place for a few passes rather than blink
        // out as tiles stream in. It may sit closer than the group's minimum distance while it does, but
        // NOT on top of a neighbour - an overlap granted here stays until something else moves.
        if (labelInfo.wasVisible && label->getCalloutFailures() < style->calloutPersistPasses) {
            // Held over ON THE LINE the band asks for, and nowhere else: a name kept at its old
            // lift is a name off the row, and the row is the whole point of the band.
            if (envelopeAt(lift) && testGridOverlap(labelInfo)) {
                label->setCalloutFailures(label->getCalloutFailures() + 1);
                return true;
            }
        }
        label->setCalloutFailures(0);
        return false;
    }

    bool LabelCuller::placeAnchoredLabel(LabelInfo& labelInfo, const std::function<bool(const LabelInfo&)>& testGroupDistance) {
        const std::shared_ptr<Label>& label = labelInfo.label;
        int count = static_cast<int>(labelInfo.variants.size());

        // The side the label already holds is tried first, so a pass that changes nothing else leaves it
        // there - a name changing side under a moving camera reads as flicker. Never the icon-only
        // variant: it always fits, so a label that fell back to it once would keep it for good.
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

        for (int index : candidates) {
            takeVariant(labelInfo, index);
            if (testGridOverlap(labelInfo) && testGroupDistance(labelInfo)) {
                return true;
            }
        }

        // Nothing free. Leave it where it was, so that a label on its way out fades where it last
        // was instead of jumping to the last side it tried.
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
