#ifndef _CARTO_COMPRESSIONUTILS_H_
#define _CARTO_COMPRESSIONUTILS_H_

#include <vector>
#include <cstring>

#include <cstdint>

#ifdef HAVE_ZSTD
#include <zstd.h>
#endif
#ifdef HAVE_BROTLI
#include <brotli/decode.h>
#endif

#include <stdext/zlib.h>

namespace carto::mvt {

    /**
     * Compression utilities namespace for handling various compression formats.
     */
    namespace compression {

        inline bool is_gzip(const unsigned char* data, std::size_t size) {
            return size >= 2 && data[0] == 0x1F && data[1] == 0x8B;
        }

#ifdef HAVE_ZSTD
        inline bool is_zstd(const unsigned char* data, std::size_t size) {
            // ZSTD frame magic is 0xFD2FB528 (little-endian bytes 28 B5 2F FD)
            return size >= 4 && data[0] == 0x28 && static_cast<unsigned char>(data[1]) == 0xB5
                && static_cast<unsigned char>(data[2]) == 0x2F && static_cast<unsigned char>(data[3]) == 0xFD;
        }
#endif

#ifdef HAVE_BROTLI
        // Fast brotli format detection using streaming API.
        // Returns true only when the decoder accepts the input as brotli stream (does not fully decode).
        inline bool is_brotli(const unsigned char* data, std::size_t size) {
            if (!data || size == 0) return false;

            BrotliDecoderState* state = BrotliDecoderCreateInstance(nullptr, nullptr, nullptr);
            if (!state) return false;

            const uint8_t* next_in = reinterpret_cast<const uint8_t*>(data);
            size_t avail_in = size;
            uint8_t outbuf[1];
            uint8_t* next_out = outbuf;
            size_t avail_out = 1;
            BrotliDecoderResult res = BrotliDecoderDecompressStream(state, &avail_in, &next_in, &avail_out, &next_out, nullptr);

            bool ok = false;
            if (res == BROTLI_DECODER_RESULT_SUCCESS) {
                ok = true;
            }
            else if (res == BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT) {
                // produced output (filled outbuf) => valid brotli stream
                ok = (avail_out == 0);
            }
            else {
                // NEEDS_MORE_INPUT or ERROR => not a valid brotli stream (or incomplete)
                ok = false;
            }

            BrotliDecoderDestroyInstance(state);
            return ok;
        }

        /**
         * Decompress brotli-compressed data.
         * @param compressedData Pointer to the compressed data.
         * @param compressedSize Size of the compressed data.
         * @param uncompressedData Output vector for the decompressed data.
         * @return True if decompression was successful, false otherwise.
         */
        inline bool inflate_brotli(const unsigned char* compressedData, std::size_t compressedSize, std::vector<unsigned char>& uncompressedData) {
            if (!compressedData || compressedSize == 0) return false;

            // quick sanity: require detection before heavy decompression (see is_brotli)
            if (!is_brotli(compressedData, compressedSize)) return false;

            // Start with a reasonable estimate for output size
            std::size_t bufferSize = compressedSize * 4 + 1024;
            uncompressedData.resize(bufferSize);

            size_t decodedSize = bufferSize;
            BrotliDecoderResult result = BrotliDecoderDecompress(compressedSize, compressedData, &decodedSize, uncompressedData.data());

            if (result == BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT) {
                // Try a larger buffer
                bufferSize = compressedSize * 50 + 16384;
                uncompressedData.resize(bufferSize);
                decodedSize = bufferSize;
                result = BrotliDecoderDecompress(compressedSize, compressedData, &decodedSize, uncompressedData.data());
            }

            if (result == BROTLI_DECODER_RESULT_SUCCESS) {
                uncompressedData.resize(decodedSize);
                return true;
            }

            return false;
        }
#endif
#ifdef HAVE_ZSTD
        /**
         * Decompress zstd-compressed data.
         * @param compressedData Pointer to the compressed data.
         * @param compressedSize Size of the compressed data.
         * @param uncompressedData Output vector for the decompressed data.
         * @return True if decompression was successful, false otherwise.
         */
        inline bool inflate_zstd(const unsigned char* compressedData, std::size_t compressedSize, std::vector<unsigned char>& uncompressedData) {
            // Get the decompressed size from the frame header
            unsigned long long const frameContentSize = ZSTD_getFrameContentSize(compressedData, compressedSize);

            if (frameContentSize == ZSTD_CONTENTSIZE_ERROR || frameContentSize == ZSTD_CONTENTSIZE_UNKNOWN) {
                // If size is unknown, use an estimate
                std::size_t bufferSize = compressedSize * 10;
                uncompressedData.resize(bufferSize);

                std::size_t const result = ZSTD_decompress(
                    uncompressedData.data(),
                    bufferSize,
                    compressedData,
                    compressedSize
                );

                if (ZSTD_isError(result)) {
                    // Try with larger buffer
                    bufferSize = compressedSize * 50;
                    uncompressedData.resize(bufferSize);

                    std::size_t const result2 = ZSTD_decompress(
                        uncompressedData.data(),
                        bufferSize,
                        compressedData,
                        compressedSize
                    );

                    if (ZSTD_isError(result2)) {
                        return false;
                    }

                    uncompressedData.resize(result2);
                    return true;
                }

                uncompressedData.resize(result);
                return true;
            } else {
                // We know the exact size
                uncompressedData.resize(frameContentSize);

                std::size_t const result = ZSTD_decompress(
                    uncompressedData.data(),
                    frameContentSize,
                    compressedData,
                    compressedSize
                );

                if (ZSTD_isError(result)) {
                    return false;
                }

                return true;
            }
        }
#endif

        /**
         * Decompress a tile payload, whatever it is wrapped in. Detects the container from its
         * header rather than attempting trial decompression, so an already-uncompressed tile costs
         * nothing.
         * @param data Pointer to the tile data.
         * @param size Size of the tile data.
         * @param uncompressedData Output vector, only written when this returns true.
         * @return True if the data was compressed and has been inflated, false if it is raw.
         */
        inline bool inflate_tile(const unsigned char* data, std::size_t size, std::vector<unsigned char>& uncompressedData) {
            if (!data || size == 0) {
                return false;
            }
            if (is_gzip(data, size)) {
                return zlib::inflate_gzip(data, size, uncompressedData);
            }
#ifdef HAVE_ZSTD
            if (is_zstd(data, size)) {
                return inflate_zstd(data, size, uncompressedData);
            }
#endif
#ifdef HAVE_BROTLI
            if (is_brotli(data, size)) {
                return inflate_brotli(data, size, uncompressedData);
            }
#endif
            return false;
        }

    }

}

#endif
