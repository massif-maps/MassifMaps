#ifndef _MASSIF_MAPNIKVT_MBVTSUBTILE_H_
#define _MASSIF_MAPNIKVT_MBVTSUBTILE_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace massif::mvt {
    namespace subtile_detail {
        struct Reader {
            const unsigned char* p;
            const unsigned char* end;

            bool varint(std::uint64_t& value) {
                value = 0;
                for (int shift = 0; shift < 64 && p < end; shift += 7) {
                    unsigned char byte = *p++;
                    value |= static_cast<std::uint64_t>(byte & 0x7f) << shift;
                    if (!(byte & 0x80)) {
                        return true;
                    }
                }
                return false;
            }

            // one field; its value is [value, p) afterwards, a length prefix excluded
            bool field(std::uint32_t& tag, int& wireType, const unsigned char*& value) {
                std::uint64_t key, length;
                if (!varint(key)) {
                    return false;
                }
                tag = static_cast<std::uint32_t>(key >> 3);
                wireType = static_cast<int>(key & 7);
                value = p;
                switch (wireType) {
                case 0: if (!varint(length)) return false; break;
                case 1: p += 8; break;
                case 2: if (!varint(length) || length > static_cast<std::uint64_t>(end - p)) return false; value = p; p += length; break;
                case 5: p += 4; break;
                default: return false;
                }
                return p <= end;
            }
        };

        inline void writeVarint(std::string& out, std::uint64_t value) {
            while (value >= 0x80) {
                out.push_back(static_cast<char>((value & 0x7f) | 0x80));
                value >>= 7;
            }
            out.push_back(static_cast<char>(value));
        }

        inline void writeBytes(std::string& out, std::uint32_t tag, const std::string& bytes) {
            writeVarint(out, (static_cast<std::uint64_t>(tag) << 3) | 2);
            writeVarint(out, bytes.size());
            out += bytes;
        }

        inline std::int64_t unzigzag(std::uint64_t v) { return static_cast<std::int64_t>(v >> 1) ^ -static_cast<std::int64_t>(v & 1); }
        inline std::uint64_t zigzag(std::int64_t v) { return (static_cast<std::uint64_t>(v) << 1) ^ static_cast<std::uint64_t>(v >> 63); }

        // A feature's cursor starts at the origin, so moving it moves only its first MoveTo.
        // False when the moved feature misses [lo, hi] on either axis.
        inline bool shiftGeometry(Reader reader, std::int64_t dx, std::int64_t dy, std::int64_t lo, std::int64_t hi, std::string& out) {
            std::vector<std::uint64_t> values;
            for (std::uint64_t v; reader.p < reader.end && reader.varint(v); ) {
                values.push_back(v);
            }
            std::int64_t x = -dx, y = -dy;
            std::int64_t minX = std::numeric_limits<std::int64_t>::max(), minY = minX;
            std::int64_t maxX = std::numeric_limits<std::int64_t>::min(), maxY = maxX;
            bool moved = false;
            for (std::size_t i = 0; i < values.size(); ) {
                std::uint64_t command = values[i] & 7, count = values[i] >> 3;
                i++;
                for (std::uint64_t n = 0; command != 7 && n < count && i + 1 < values.size(); n++, i += 2) {
                    x += unzigzag(values[i]);
                    y += unzigzag(values[i + 1]);
                    if (!moved) {
                        values[i] = zigzag(x);
                        values[i + 1] = zigzag(y);
                        moved = true;
                    }
                    minX = std::min(minX, x); maxX = std::max(maxX, x);
                    minY = std::min(minY, y); maxY = std::max(maxY, y);
                }
            }
            if (!moved || maxX < lo || minX > hi || maxY < lo || minY > hi) {
                return false;
            }
            for (std::uint64_t value : values) {
                writeVarint(out, value);
            }
            return true;
        }
    }

    // The child tile (x, y) `dz` zooms under an uncompressed vector tile: each layer's extent divided by
    // 2^dz and its geometry shifted, features outside the child plus a 1/8 tile buffer dropped.
    // Empty when the tile does not parse or an extent does not divide.
    inline std::vector<unsigned char> subtileMBVT(const std::vector<unsigned char>& data, int dz, int x, int y) {
        using namespace subtile_detail;
        if (dz <= 0 || dz >= 24) {
            return {};
        }
        std::string out;
        Reader tile { data.data(), data.data() + data.size() };
        std::uint32_t tag;
        int wireType;
        const unsigned char* value;
        while (tile.p < tile.end) {
            const unsigned char* start = tile.p;
            if (!tile.field(tag, wireType, value)) {
                return {};
            }
            if (tag != 3 || wireType != 2) {
                out.append(reinterpret_cast<const char*>(start), tile.p - start);
                continue;
            }
            const Reader layerBytes { value, tile.p };
            std::uint64_t extent = 4096;
            for (Reader scan = layerBytes; scan.p < scan.end; ) {
                if (!scan.field(tag, wireType, value)) {
                    return {};
                }
                if (tag == 5 && wireType == 0) {
                    Reader { value, scan.p }.varint(extent);
                }
            }
            if (extent % (std::uint64_t(1) << dz) != 0) {
                return {};
            }
            std::int64_t childExtent = static_cast<std::int64_t>(extent >> dz);
            std::int64_t mask = (std::int64_t(1) << dz) - 1;
            std::int64_t dx = (x & mask) * childExtent, dy = (y & mask) * childExtent;
            std::string layer;
            for (Reader fields = layerBytes; fields.p < fields.end; ) {
                const unsigned char* fieldStart = fields.p;
                if (!fields.field(tag, wireType, value)) {
                    return {};
                }
                if (tag == 5) {
                    continue;
                }
                if (tag != 2 || wireType != 2) {
                    layer.append(reinterpret_cast<const char*>(fieldStart), fields.p - fieldStart);
                    continue;
                }
                std::string feature;
                bool keep = false;
                for (Reader parts { value, fields.p }; parts.p < parts.end; ) {
                    const unsigned char* partStart = parts.p;
                    const unsigned char* partValue;
                    if (!parts.field(tag, wireType, partValue)) {
                        return {};
                    }
                    if (tag == 4 && wireType == 2) {
                        std::string geometry;
                        keep = shiftGeometry(Reader { partValue, parts.p }, dx, dy, -childExtent / 8, childExtent + childExtent / 8, geometry);
                        writeBytes(feature, 4, geometry);
                    } else {
                        feature.append(reinterpret_cast<const char*>(partStart), parts.p - partStart);
                    }
                }
                if (keep) {
                    writeBytes(layer, 2, feature);
                }
            }
            writeVarint(layer, 5 << 3);
            writeVarint(layer, static_cast<std::uint64_t>(childExtent));
            writeBytes(out, 3, layer);
        }
        return std::vector<unsigned char>(out.begin(), out.end());
    }
}

#endif
