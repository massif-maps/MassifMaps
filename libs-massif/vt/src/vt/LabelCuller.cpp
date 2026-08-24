#include "LabelCuller.h"
#include "RenderStats.h"

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
            // The buffer widens the AXIS, not this one edge. Measuring it against the edge let the
            // OPPOSITE edge of a rectangle - same axis, always further away than the buffer - report
            // a separating axis and return before the near edge was ever tested, so a minimum
            // distance did nothing at all for two labels offset along their own edge.
            // 'proj' is not normalized, so the gap is scaled the way the extents are.
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

    void LabelCuller::reset() {
        std::lock_guard<std::mutex> lock(_mutex);

        clearGrid();
    }

    bool LabelCuller::process(const std::vector<std::shared_ptr<Label>>& labelList, std::mutex& labelMutex) {
        std::lock_guard<std::mutex> lock(_mutex);

        VT_STAT_INC(cullerPasses);
        VT_STAT_CLOCK(cullerClock);

        // NOTE: the grid is intentionally NOT cleared here. One culler instance is shared
        // by all layers within a single placement pass (see VTLabelPlacementWorker), so
        // records must accumulate across process() calls for labels of different layers
        // to collide with each other. Each pass uses a freshly constructed culler, so no
        // stale records from previous passes can exist.

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
        BatchLock labelLock(labelMutex, LABEL_LOCK_BATCH);
        for (const std::shared_ptr<Label>& label : labelList) {
            labelLock.step();

            // Analyze only active and valid labels
            if (!label->isActive()) {
                continue;
            }

            // Capture visibility from the previous frame BEFORE updatePlacement, which may
            // reset opacity to 0 even for labels that were already visible on screen.
            bool wasVisible = label->isVisible();

            // Style max-distance: a label glyph is screen-space, so an unlimited view fills its
            // horizon band with labels drawn at full size for features kilometres away. Hiding
            // rather than skipping keeps the existing opacity animation, so the label FADES out
            // when it passes the limit and fades back in when it returns - no per-frame work, the
            // GL thread already animates opacity towards isVisible().
            const std::shared_ptr<const TileLabel::Style>& style = label->getStyle();
            bool ranked = !(style->rankFunc == FloatFunction(0.0f));
            float maxDistance = style->maxDistance;
            float distance = 0; // meters, 0 when it could not be resolved
            if ((maxDistance > 0 || ranked) && _metersToInternal > 0) {
                cglib::vec3<double> position(0, 0, 0);
                if (label->calculateCenter(position)) {
                    distance = static_cast<float>(cglib::length(position - _viewState.origin) / _metersToInternal);
                    if (maxDistance > 0 && distance > maxDistance) {
                        label->setVisible(false);
                        continue;
                    }
                }
            }

            if (label->updatePlacement(_viewState)) {
                label->setOpacity(0);
            }

            if (label->isValid()) {
                float size = (style->sizeFunc)(_viewState);
                // Ranking is the label's own priority plus what the style makes of the view - the
                // one evaluation that is per label, so the one place view::distance means
                // anything. It only reorders the greedy insertion below; the drawn size and
                // colour still come from the batch, so a rank expression can never change how a
                // label looks.
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

        // Handed back around the sort: it is the one stretch of a pass long enough for the GL
        // thread to notice, and it reads no label state that thread writes.
        labelLock.release();

        // Sort active labels by priority/wasVisible/layerIndex/size/opacity.
        // Labels that were visible in the previous frame (wasVisible=true) are placed before
        // newly-appearing labels of equal priority.  This mirrors the "committed placement"
        // strategy used by MapLibre / Mapbox GL: once a label is on screen it keeps its grid
        // slot unless a strictly higher-priority label needs to displace it.  Using the
        // isVisible() boolean (captured before this frame's placement update) is more reliable
        // than opacity, which can be reset to 0 by updatePlacement() even for visible labels.
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

        // Update label visibility flag based on overlap analysis
        std::unordered_map<long long, std::vector<const LabelInfo*>> groupMap;
        groupMap.reserve(validLabelList.size());
        bool changed = false;
        // The group's minimum distance: labels of one group must not only miss each other, they must
        // stay that many pixels apart. A callout is tested for it AT EVERY ROW it tries (see
        // placeCalloutLabel) - testing it only after placement would place the label on a free row
        // and then hide it for being too close to a neighbour, which is the one outcome the
        // stacking exists to avoid.
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

            if (visible) {
                if (groupId >= 0) {
                    addGridRecord(_recordGrid, labelInfo.cullRecord);
                }
                if (groupId > 0) {
                    groupMap[groupId].push_back(&labelInfo);
                }
            }
            if (visible != label->isVisible()) {
                label->setVisible(visible);
                VT_STAT_INC(cullerVisibilityFlips);
                changed = true;
            }
        }
        VT_STAT_SPLIT(cullerNs, cullerClock);
        return changed;
    }

    cglib::vec2<int> LabelCuller::getGridIndex(const cglib::vec2<float>& pos) const {
        int x = std::max(0, std::min(GRID_RESOLUTION_X - 1, static_cast<int>(GRID_RESOLUTION_X * pos(0) / _viewState.resolution / _viewState.aspect)));
        int y = std::max(0, std::min(GRID_RESOLUTION_Y - 1, static_cast<int>(GRID_RESOLUTION_Y * pos(1) / _viewState.resolution)));
        return cglib::vec2<int>(x, y);
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

        auto envelopeAt = [this, &labelInfo, &label](float offset) {
            label->setCalloutPlacement(offset, label->calculateAnchorScreenY(_viewState));
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

        // Where it ended up last time. A callout is re-placed from scratch on every pass, and a
        // panning map runs one whenever its tile set changes, so keeping the row it already holds
        // (when it is still a legal one) is what stops a screen of names re-flowing under the
        // camera.
        float previousOffset = label->getCalloutOffset();

        // Where the label wants to sit before anything else is taken into account: either a band
        // at a fixed height on screen - which is what makes a panorama read as one row of names
        // over the ridges - or straight above its own anchor.
        if (!envelopeAt(0)) {
            return false;
        }
        float anchorY = bandAnchorY();
        float top = labelInfo.cullRecord.bounds.max(1);

        // Everything below is in SCREEN PIXELS, and so is the offset the label is given: it is
        // converted to world units at draw time against the projection at the label's own depth
        // (Label::calculatePixelToWorld), so a lift of N pixels stays N pixels while the camera
        // tilts, rises or zooms - the placement is not re-scaled under the label between passes.
        float lift = style->calloutOffset;
        if (style->calloutScreenAnchor >= 0) {
            float bandY = (1.0f - style->calloutScreenAnchor) * _viewState.resolution;
            lift = std::max(lift, bandY - anchorY);
        }
        // Whatever the band asks for, the label has to stay on screen: it is lifted away from its
        // anchor, so unlike every other label its own position is no evidence that it is in view.
        // The margin is a CONSTANT, not a share of the label: it also caps a label the band placed
        // correctly, and a margin proportional to the label's own height would then push long names
        // further down than short ones - the row stops being a row.
        float maxLift = _viewState.resolution - top - SCREEN_EDGE_MARGIN;
        // Rows may go down (negative step), but never below the lift the style asks for: the label
        // belongs ABOVE its feature, and its leader line only exists while it is.
        float minLift = std::max(style->calloutOffset, SCREEN_EDGE_MARGIN - labelInfo.cullRecord.bounds.min(1));
        // A summit already so high on screen that its name would not fit above it has no place for
        // that name: drop it. Pulling the label back down to the screen edge instead put it BELOW
        // its own summit - off the band the style asks for, and with a leader line pointing down
        // (or, at a negative lift, no line at all).
        if (lift > maxLift || minLift > maxLift) {
            label->setCalloutFailures(0);
            return false;
        }
        lift = std::max(lift, minLift);

        float step = (style->calloutStep > 0 ? style->calloutStep : labelInfo.size * 1.2f);

        // The row it already holds is tried first, as long as it is still one this pass would
        // offer: a label that keeps changing row while the camera moves reads as flicker even
        // though it never disappears.
        if (labelInfo.wasVisible && previousOffset > 0) {
            if (previousOffset >= lift - 0.5f && previousOffset <= maxLift + 0.5f) {
                if (envelopeAt(previousOffset) && testGridOverlap(labelInfo) && testGroupDistance(labelInfo)) {
                    label->setCalloutFailures(0);
                    return true;
                }
            }
        }

        // Then row by row, in the direction the style's step points (DOWN for a negative one -
        // a band pinned to the top of the screen has no room above it, and stacking upwards there
        // is what turned the top row into a pile), until the screen is free. Stepping instead of
        // hiding is the whole point of the orientation: a summit that loses its slot to a nearer
        // one still gets its name, one line further along.
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

        // Nothing free. A name already on screen may hold its place for a few passes rather than
        // blink out and back in as tiles stream in under a moving camera (text-callout-persist).
        // It may sit closer to its neighbours than the group's minimum distance while it does, but
        // it may NOT sit on top of one: a placement pass only runs when the draw data changes, so
        // an overlap granted here stays on screen until something else moves.
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

        // The side the label already holds is tried first, so a pass that changes nothing else
        // leaves it there - a name that changes side under a moving camera reads as flicker. Never
        // the icon-only variant: it is smaller than every other one, so it always fits, and a label
        // that fell back to it once would keep it for good.
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
