
#include "PMTilesUtils.h"
#include "components/Exceptions.h"
#include "utils/Log.h"

#include <cstring>
#include <algorithm>
#include <zlib.h>
//#ifdef HAVE_BROTLI
#include <brotli/decode.h>
//#endif
#ifdef HAVE_ZSTD
#include <zstd.h>
#endif

namespace massif {
namespace pmtiles {

    namespace {
        // Helper function for Hilbert curve quadrant rotation
        void rotateQuadrant(int n, int& x, int& y, int rx, int ry) {
            if (ry == 0) {
                if (rx == 1) {
                    x = n - 1 - x;
                    y = n - 1 - y;
                }
                int t = x;
                x = y;
                y = t;
            }
        }

        // The gzip trailer holds the uncompressed size mod 2^32: sizing from it makes the common case
        // a single inflate into the final buffer. A multi-member or oversized stream grows from there.
        std::vector<uint8_t> inflateGzip(const std::vector<uint8_t>& data) {
            z_stream stream;
            std::memset(&stream, 0, sizeof(stream));
            // windowBits = 15 + 16 for gzip
            if (inflateInit2(&stream, 15 + 16) != Z_OK) {
                throw GenericException("Failed to initialize gzip decompression");
            }

            std::size_t expectedSize = data.size() * 4;
            if (data.size() >= 18) {
                const uint8_t* trailer = data.data() + data.size() - 4;
                std::size_t declaredSize = static_cast<std::size_t>(trailer[0]) | (static_cast<std::size_t>(trailer[1]) << 8) | (static_cast<std::size_t>(trailer[2]) << 16) | (static_cast<std::size_t>(trailer[3]) << 24);
                // deflate cannot expand past ~1032:1, so a larger claim is not a size
                if (declaredSize > 0 && declaredSize <= data.size() * 1032) {
                    expectedSize = declaredSize;
                }
            }

            std::vector<uint8_t> result(std::max<std::size_t>(expectedSize, 1));
            stream.avail_in = static_cast<uInt>(data.size());
            stream.next_in = const_cast<uint8_t*>(data.data());

            int ret;
            do {
                if (stream.avail_out == 0) {
                    std::size_t produced = static_cast<std::size_t>(stream.total_out);
                    if (produced == result.size()) {
                        result.resize(result.size() * 2);
                    }
                    stream.next_out = result.data() + produced;
                    stream.avail_out = static_cast<uInt>(result.size() - produced);
                }
                ret = inflate(&stream, Z_NO_FLUSH);
                if (ret == Z_STREAM_ERROR || ret == Z_DATA_ERROR || ret == Z_MEM_ERROR || ret == Z_NEED_DICT) {
                    inflateEnd(&stream);
                    throw GenericException("gzip decompression failed");
                }
                if (ret == Z_BUF_ERROR && stream.avail_in == 0 && stream.avail_out != 0) {
                    inflateEnd(&stream);
                    throw GenericException("gzip decompression failed: truncated input");
                }
            } while (ret != Z_STREAM_END);

            result.resize(static_cast<std::size_t>(stream.total_out));
            inflateEnd(&stream);
            return result;
        }
    }

    Header readHeader(const uint8_t* headerBytes) {
        // Check magic number "PMTiles"
        if (std::memcmp(headerBytes, "PMTiles", 7) != 0) {
            throw GenericException("Invalid PMTiles magic number");
        }
        
        // Check version (must be 3)
        if (headerBytes[7] != 3) {
            throw GenericException("Unsupported PMTiles version (only v3 is supported)");
        }
        
        Header header;
        
        // Helper lambda to read little-endian uint64
        auto readUInt64 = [](const uint8_t* data) -> uint64_t {
            return static_cast<uint64_t>(data[0]) |
                   (static_cast<uint64_t>(data[1]) << 8) |
                   (static_cast<uint64_t>(data[2]) << 16) |
                   (static_cast<uint64_t>(data[3]) << 24) |
                   (static_cast<uint64_t>(data[4]) << 32) |
                   (static_cast<uint64_t>(data[5]) << 40) |
                   (static_cast<uint64_t>(data[6]) << 48) |
                   (static_cast<uint64_t>(data[7]) << 56);
        };
        
        // Helper lambda to read little-endian int32
        auto readInt32 = [](const uint8_t* data) -> int32_t {
            return static_cast<int32_t>(
                static_cast<uint32_t>(data[0]) |
                (static_cast<uint32_t>(data[1]) << 8) |
                (static_cast<uint32_t>(data[2]) << 16) |
                (static_cast<uint32_t>(data[3]) << 24)
            );
        };
        
        header.rootDirectoryOffset = readUInt64(&headerBytes[8]);
        header.rootDirectoryLength = readUInt64(&headerBytes[16]);
        header.metadataOffset = readUInt64(&headerBytes[24]);
        header.metadataLength = readUInt64(&headerBytes[32]);
        header.leafDirectoriesOffset = readUInt64(&headerBytes[40]);
        header.leafDirectoriesLength = readUInt64(&headerBytes[48]);
        header.tileDataOffset = readUInt64(&headerBytes[56]);
        header.tileDataLength = readUInt64(&headerBytes[64]);
        header.numAddressedTiles = readUInt64(&headerBytes[72]);
        header.numTileEntries = readUInt64(&headerBytes[80]);
        header.numTileContents = readUInt64(&headerBytes[88]);
        header.clustered = headerBytes[96];
        header.internalCompression = headerBytes[97];
        header.tileCompression = headerBytes[98];
        header.tileType = headerBytes[99];
        header.minZoom = headerBytes[100];
        header.maxZoom = headerBytes[101];
        
        // Read positions (4 bytes for longitude, 4 bytes for latitude = 8 bytes per position)
        int32_t minLonE7 = readInt32(&headerBytes[102]);
        int32_t minLatE7 = readInt32(&headerBytes[106]);
        int32_t maxLonE7 = readInt32(&headerBytes[110]);
        int32_t maxLatE7 = readInt32(&headerBytes[114]);
        
        header.minLon = minLonE7 / 10000000.0;
        header.minLat = minLatE7 / 10000000.0;
        header.maxLon = maxLonE7 / 10000000.0;
        header.maxLat = maxLatE7 / 10000000.0;
        
        header.centerZoom = headerBytes[118];
        int32_t centerLonE7 = readInt32(&headerBytes[119]);
        int32_t centerLatE7 = readInt32(&headerBytes[123]);
        header.centerLon = centerLonE7 / 10000000.0;
        header.centerLat = centerLatE7 / 10000000.0;
        
        return header;
    }

    std::vector<uint8_t> decompressData(std::vector<uint8_t>&& data, uint8_t compression) {
        if (compression == 0x01 || compression == 0x00) {
            return std::move(data);
        }
        return decompressData(static_cast<const std::vector<uint8_t>&>(data), compression);
    }

    std::vector<uint8_t> decompressData(const std::vector<uint8_t>& data, uint8_t compression) {
        // Compression: 0x00=Unknown, 0x01=None, 0x02=gzip, 0x03=brotli, 0x04=zstd
        
        if (compression == 0x01 || compression == 0x00) {
            // No compression or unknown (treat as uncompressed)
            return data;
        }
        else if (compression == 0x02) {
            return inflateGzip(data);
        }
        else if (compression == 0x03) {
            // Brotli decompression
            // Initial buffer size: estimate 10x compression ratio (typical for map tiles)
            size_t maxOutputSize = data.size() * 10;
            std::vector<uint8_t> result(maxOutputSize);
            size_t decodedSize = maxOutputSize;
            
            BrotliDecoderResult status = BrotliDecoderDecompress(
                data.size(),
                data.data(),
                &decodedSize,
                result.data()
            );
            
            // If buffer was too small, retry with larger buffer
            // Fallback buffer size: 50x for edge cases with high compression ratios
            if (status == BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT) {
                maxOutputSize = data.size() * 50;
                result.resize(maxOutputSize);
                decodedSize = maxOutputSize;
                
                status = BrotliDecoderDecompress(
                    data.size(),
                    data.data(),
                    &decodedSize,
                    result.data()
                );
            }
            
            if (status != BROTLI_DECODER_RESULT_SUCCESS) {
                Log::Errorf("PMTiles: Brotli decompression failed with status code %d", static_cast<int>(status));
                throw GenericException("Brotli decompression failed");
            }
            
            result.resize(decodedSize);
            return result;
        }
#ifdef HAVE_ZSTD
        else if (compression == 0x04) {
            // Zstandard decompression
            unsigned long long const decompressedSize = ZSTD_getFrameContentSize(data.data(), data.size());
            
            if (decompressedSize == ZSTD_CONTENTSIZE_ERROR) {
                Log::Error("PMTiles: Invalid zstd compressed data");
                throw GenericException("Invalid zstd compressed data");
            }
            else if (decompressedSize == ZSTD_CONTENTSIZE_UNKNOWN) {
                // Size unknown, use heuristic
                // Initial buffer size: estimate 10x compression ratio (typical for map tiles)
                size_t maxOutputSize = data.size() * 10;
                std::vector<uint8_t> result(maxOutputSize);
                
                size_t actualSize = ZSTD_decompress(result.data(), maxOutputSize, data.data(), data.size());
                
                if (ZSTD_isError(actualSize)) {
                    // Try with larger buffer
                    // Fallback buffer size: 50x for edge cases with high compression ratios
                    maxOutputSize = data.size() * 50;
                    result.resize(maxOutputSize);
                    actualSize = ZSTD_decompress(result.data(), maxOutputSize, data.data(), data.size());
                    
                    if (ZSTD_isError(actualSize)) {
                        Log::Errorf("PMTiles: Zstandard decompression failed: %s", ZSTD_getErrorName(actualSize));
                        throw GenericException("Zstandard decompression failed");
                    }
                }
                
                result.resize(actualSize);
                return result;
            }
            else {
                // Size is known
                std::vector<uint8_t> result(decompressedSize);
                
                size_t actualSize = ZSTD_decompress(result.data(), decompressedSize, data.data(), data.size());
                
                if (ZSTD_isError(actualSize)) {
                    Log::Errorf("PMTiles: Zstandard decompression failed: %s", ZSTD_getErrorName(actualSize));
                    throw GenericException("Zstandard decompression failed");
                }
                
                return result;
            }
        }
#endif
        else {
            Log::Errorf("PMTiles: Unknown compression type: %d", compression);
            throw GenericException("Unknown compression type");
        }
    }

    std::vector<DirectoryEntry> decodeDirectory(const std::vector<uint8_t>& data) {
        size_t offset = 0;
        
        // Read number of entries
        uint64_t numEntries = readVarint(data, offset);
        
        if (numEntries == 0) {
            return std::vector<DirectoryEntry>();
        }
        
        std::vector<DirectoryEntry> entries(numEntries);
        
        // Read TileIDs (delta-encoded)
        uint64_t lastId = 0;
        for (uint64_t i = 0; i < numEntries; i++) {
            uint64_t delta = readVarint(data, offset);
            lastId += delta;
            entries[i].tileId = lastId;
        }
        
        // Read RunLengths
        for (uint64_t i = 0; i < numEntries; i++) {
            entries[i].runLength = static_cast<uint32_t>(readVarint(data, offset));
        }
        
        // Read Lengths
        for (uint64_t i = 0; i < numEntries; i++) {
            entries[i].length = static_cast<uint32_t>(readVarint(data, offset));
        }
        
        // Read Offsets
        for (uint64_t i = 0; i < numEntries; i++) {
            uint64_t value = readVarint(data, offset);
            
            if (value == 0 && i > 0) {
                // Offset is contiguous with previous entry
                entries[i].offset = entries[i - 1].offset + entries[i - 1].length;
            } else {
                entries[i].offset = value - 1;
            }
        }
        
        return entries;
    }

    uint64_t readVarint(const std::vector<uint8_t>& data, size_t& offset) {
        uint64_t result = 0;
        int shift = 0;
        
        while (offset < data.size()) {
            uint8_t byte = data[offset++];
            result |= static_cast<uint64_t>(byte & 0x7F) << shift;
            
            if ((byte & 0x80) == 0) {
                break;
            }
            
            shift += 7;
        }
        
        return result;
    }

    uint64_t zxyToTileId(int z, int x, int y) {
        if (z == 0) {
            return 0;
        }
        
        // Calculate the base TileID for this zoom level
        uint64_t baseTileId = 0;
        for (int i = 0; i < z; i++) {
            baseTileId += (1ULL << (i * 2));
        }
        
        // Hilbert curve encoding for spatial indexing
        int n = 1 << z;
        int rx, ry, s;
        int tx = x;
        int ty = y;
        uint64_t d = 0;
        
        for (s = n / 2; s > 0; s /= 2) {
            rx = (tx & s) > 0 ? 1 : 0;
            ry = (ty & s) > 0 ? 1 : 0;
            // Standard Hilbert curve quadrant encoding formula
            d += s * s * ((3 * rx) ^ ry);
            rotateQuadrant(n, tx, ty, rx, ry);
        }
        
        return baseTileId + d;
    }

    const DirectoryEntry* findDirectoryEntry(const std::vector<DirectoryEntry>& directory, uint64_t tileId) {
        // The last entry starting at or before the id: a run holding it, or the leaf pointer covering it
        auto it = std::upper_bound(directory.begin(), directory.end(), tileId, [](uint64_t id, const DirectoryEntry& entry) {
            return id < entry.tileId;
        });
        if (it == directory.begin()) {
            return nullptr;
        }
        --it;
        if (it->runLength == 0 || tileId - it->tileId < it->runLength) {
            return &*it;
        }
        return nullptr;
    }

    bool findTileEntry(const std::vector<DirectoryEntry>& directory, uint64_t tileId, DirectoryEntry& outEntry) {
        const DirectoryEntry* entry = findDirectoryEntry(directory, tileId);
        if (!entry || entry->runLength == 0) {
            return false;
        }
        outEntry = *entry;
        return true;
    }

} // namespace pmtiles
} // namespace massif
