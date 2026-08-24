#include "TileDownloadInfo.h"

#include <iomanip>
#include <sstream>

namespace massif {

    TileDownloadInfo::TileDownloadInfo(int tileCount, float progress, const MapTile& tile) :
        _tileCount(tileCount),
        _progress(progress),
        _tile(tile)
    {
    }

    TileDownloadInfo::~TileDownloadInfo() {
    }

    int TileDownloadInfo::getTileCount() const {
        return _tileCount;
    }

    float TileDownloadInfo::getProgress() const {
        return _progress;
    }

    const MapTile& TileDownloadInfo::getTile() const {
        return _tile;
    }

    std::string TileDownloadInfo::toString() const {
        std::stringstream ss;
        ss << std::setiosflags(std::ios::fixed);
        ss << "TileDownloadInfo [tileCount=" << _tileCount << ", progress=" << _progress
           << ", tile=" << _tile.toString() << "]";
        return ss.str();
    }

}
