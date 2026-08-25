#include "BillboardPlacementWorker.h"
#include "components/Options.h"
#include "renderers/BillboardRenderer.h"
#include "renderers/MapRenderer.h"
#include "terrain/ElevationManager.h"
#include "renderers/drawdatas/BillboardDrawData.h"
#include "renderers/drawdatas/NMLModelDrawData.h"
#include "utils/Log.h"
#include "utils/GeomUtils.h"
#include "utils/ThreadUtils.h"
#include "vectorelements/Billboard.h"

#include <algorithm>

namespace massif {

    BillboardPlacementWorker::BillboardPlacementWorker() :
        _stop(false),
        _idle(false),
        _kdTree(),
        _pendingWakeup(false),
        _wakeupTime(std::chrono::steady_clock::now() + std::chrono::hours(24)),
        _mapRenderer(),
        _condition(),
        _mutex()
    {
    }
    
    BillboardPlacementWorker::~BillboardPlacementWorker() {
    }
        
    void BillboardPlacementWorker::setComponents(const std::weak_ptr<MapRenderer>& mapRenderer, const std::shared_ptr<BillboardPlacementWorker>& worker) {
        _mapRenderer = mapRenderer;
        // When the map component gets destroyed all threads get detatched. Detatched threads need their worker objects to be alive,
        // so worker objects need to keep references to themselves, until the loop finishes.
        _worker = worker;
    }
        
    void BillboardPlacementWorker::init(int delayTime) {
        std::lock_guard<std::mutex> lock(_mutex);
        _idle = false;
        _pendingWakeup = true;
        _wakeupTime = std::min(_wakeupTime, std::chrono::steady_clock::now() + std::chrono::milliseconds(delayTime));
        _condition.notify_one();
    }
    
    void BillboardPlacementWorker::stop() {
        std::lock_guard<std::mutex> lock(_mutex);
        _stop = true;
        _condition.notify_all();
    }
    
    bool BillboardPlacementWorker::isIdle() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _idle;
    }
        
    void BillboardPlacementWorker::operator ()() {
        run();
        _worker.reset();
    }
    
    void BillboardPlacementWorker::run() {
        ThreadUtils::SetThreadPriority(ThreadPriority::LOW);
    
        while (true) {
            bool run = false;
            {
                std::unique_lock<std::mutex> lock(_mutex);

                if (_stop) {
                    return;
                }

                std::chrono::steady_clock::time_point currentTime = std::chrono::steady_clock::now();
                if (_wakeupTime - currentTime < std::chrono::milliseconds(1)) {
                    run = true;
                    _pendingWakeup = false;
                    _wakeupTime = currentTime + std::chrono::hours(24);
                }

                if (!run) {
                    _idle = !_pendingWakeup;
                    _condition.wait_for(lock, _wakeupTime - currentTime);
                    _idle = false;
                }
            }

            if (run) {
                calculateBillboardPlacement();
            }
        }
    }
    
    bool BillboardPlacementWorker::calculateBillboardPlacement() {
        std::shared_ptr<MapRenderer> mapRenderer = _mapRenderer.lock();
        if (!mapRenderer) {
            return false;
        }

        std::vector<std::shared_ptr<BillboardDrawData> > billboardDrawDatas = mapRenderer->getBillboardDrawDatas();

        // Terrain occlusion pass: test the sight line from the camera to each billboard anchor
        // against the terrain. State-dependent up-offsets provide hysteresis against flickering.
        if (calculateTerrainOcclusion(billboardDrawDatas, mapRenderer)) {
            mapRenderer->requestRedraw();
        }

        bool calculate = false;
        for (const std::shared_ptr<BillboardDrawData>& drawData : billboardDrawDatas) {
            if (drawData->isHideIfOverlapped()) {
                calculate = true;
                break;
            }
        }

        if (!calculate) {
            return false;
        }

        ViewState viewState = mapRenderer->getViewState();
        const cglib::mat4x4<double>& mvpMat = viewState.getModelviewProjectionMat();
        const cglib::mat4x4<float>& rteMVPMat = viewState.getRTEModelviewProjectionMat();

        // Sort draw datas
        auto distanceComparator = [](const std::shared_ptr<BillboardDrawData>& drawData1, const std::shared_ptr<BillboardDrawData>& drawData2) {
            // Sort by overlappability
            if (drawData1->isHideIfOverlapped() != drawData2->isHideIfOverlapped()) {
                return drawData2->isHideIfOverlapped() < drawData1->isHideIfOverlapped();
            }

            // Sort using DrawData ordering
            return drawData1->isBefore(*drawData2);
        };
        std::stable_sort(billboardDrawDatas.begin(), billboardDrawDatas.end(), distanceComparator);
        std::reverse(billboardDrawDatas.begin(), billboardDrawDatas.end());

        // Calculate billboard screen coordinates, add envelopes to the tree
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _kdTree.clear();
        }

        bool changed = false;
        for (const std::shared_ptr<BillboardDrawData>& drawData : billboardDrawDatas) {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_stop) {
                return false;
            }

            // Calculate billboard world coordinates
            std::vector<MapPos> points;
            if (auto nmlDrawData = std::dynamic_pointer_cast<NMLModelDrawData>(drawData)) {
                cglib::mat4x4<double> modelMat;
                if (!BillboardRenderer::CalculateNMLModelMatrix(*nmlDrawData, viewState, modelMat)) {
                    continue;
                }

                points.reserve(8);
                for (int i = 0; i < 8; i++) {
                    const cglib::bbox3<float>& bounds = nmlDrawData->getSourceModelBounds();
                    cglib::vec3<double> pos = cglib::transform_point(cglib::vec3<double>(i & 1 ? bounds.max(0) : bounds.min(0), i & 2 ? bounds.max(1) : bounds.min(1), i & 4 ? bounds.max(2) : bounds.min(2)), mvpMat * modelMat);
                    points.emplace_back(pos(0), pos(1), 0);
                }
            } else {
                std::vector<float> coordBuf(12);
                if (!BillboardRenderer::CalculateBillboardCoords(*drawData, viewState, coordBuf, 0)) {
                    continue;
                }

                points.reserve(4);
                for (int i = 0; i < 4; i++) {
                    cglib::vec3<float> pos = cglib::transform_point(cglib::vec3<float>(coordBuf[i * 3 + 0], coordBuf[i * 3 + 1], coordBuf[i * 3 + 2]), rteMVPMat);
                    points.emplace_back(pos(0), pos(1), 0);
                }
            }

            // Calculate bounding box and envelope
            cglib::bbox3<double> bounds = cglib::bbox3<double>::smallest();
            for (const MapPos& point : points) {
                bounds.add(cglib::vec3<double>(point.getX(), point.getY(), 0));
            }
            MapEnvelope envelope(GeomUtils::CalculateConvexHull(points));

            // Do overlap tests
            bool overlapped = false;
            if (drawData->isHideIfOverlapped()) {
                // Check that there are higher priority billboards overlapping with this one
                const std::vector<MapEnvelope>& overlappedEnvelopes = _kdTree.query(bounds);
                for (const MapEnvelope& overlappedEnvelope : overlappedEnvelopes) {
                    if (overlappedEnvelope.intersects(envelope)) {
                        // Overlapping detected, hide this billboard
                        drawData->setOverlapping(true);
                        changed = true;
                        overlapped = true;
                        break;
                    }
                }
            }
            
            if (!overlapped) {
                drawData->setOverlapping(false);
                changed = true;
                if (drawData->isCausesOverlap()) {
                    _kdTree.insert(bounds, envelope);
                }
            }
        }

        if (changed) {
            mapRenderer->requestRedraw();
        }

        return true;
    }

    bool BillboardPlacementWorker::calculateTerrainOcclusion(const std::vector<std::shared_ptr<BillboardDrawData> >& billboardDrawDatas, const std::shared_ptr<MapRenderer>& mapRenderer) const {
        std::shared_ptr<ElevationManager> elevationManager;
        if (std::shared_ptr<Options> options = mapRenderer->getOptions()) {
            if (options->getRenderProjectionMode() == RenderProjectionMode::RENDER_PROJECTION_MODE_PLANAR) {
                if (std::shared_ptr<TerrainOptions> terrainOptions = options->getTerrainOptions()) {
                    if (terrainOptions->isActive() && terrainOptions->isBillboardOcclusionEnabled()) {
                        elevationManager = terrainOptions->getElevationManager();
                    }
                }
            }
        }

        bool changed = false;
        ViewState viewState = mapRenderer->getViewState();
        cglib::vec3<double> cameraPos = viewState.getCameraPos();
        for (const std::shared_ptr<BillboardDrawData>& drawData : billboardDrawDatas) {
            bool occluded = false;
            if (elevationManager) {
                bool wasOccluded = drawData->isTerrainOccluded();
                cglib::vec3<double> pos = drawData->getPos();
                double dist = cglib::length(pos - cameraPos);
                // Hysteresis: a visible billboard is tested at a raised point (harder to occlude),
                // an occluded one close to its anchor (harder to become visible again)
                double upOffset = dist * (wasOccluded ? 0.001 : 0.005);
                cglib::vec3<double> target = pos + cglib::vec3<double>(0, 0, upOffset);
                cglib::ray3<double> ray(cameraPos, target - cameraPos);
                double t = 0;
                occluded = elevationManager->intersectRay(ray, t) && t > 0 && t < 0.995;
            }
            if (occluded != drawData->isTerrainOccluded()) {
                drawData->setTerrainOccluded(occluded);
                changed = true;
            }
        }
        return changed;
    }

}
