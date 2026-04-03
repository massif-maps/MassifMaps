
#ifndef _CARTO_COMPRESSIONUTILS_H_
#define _CARTO_COMPRESSIONUTILS_H_

#include <vector>
#include <cstring>

#include <brotli/decode.h>

#ifdef HAVE_ZSTD
#include <zstd.h>
#endif

namespace carto::mvt {

    /**
     * Compression utilities namespace for handling various compression formats.
     */
    namespace compression {

        /**
         * Decompress brotli-compressed data.
         * @param compressedData Pointer to the compressed data.
         * @param compressedSize Size of the compressed data.
         * @param uncompressedData Output vector for the decompressed data.
         * @return True if decompression was successful, false otherwise.
         */
        inline bool inflate_brotli(const unsigned char* compressedData, std::size_t compressedSize, std::vector<unsigned char>& uncompressedData) {
            // Initial buffer size estimate (10x compressed size)
            std::size_t bufferSize = compressedSize * 10;
            uncompressedData.resize(bufferSize);
            
            std::size_t decodedSize = bufferSize;
            BrotliDecoderResult result = BrotliDecoderDecompress(
                compressedSize,
                compressedData,
                &decodedSize,
                uncompressedData.data()
            );
            
            // If we need more output space, try with 50x compressed size
            if (result == BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT) {
                bufferSize = compressedSize * 50;
                uncompressedData.resize(bufferSize);
                decodedSize = bufferSize;
                
                result = BrotliDecoderDecompress(
                    compressedSize,
                    compressedData,
                    &decodedSize,
                    uncompressedData.data()
                );
            }
            
            if (result == BROTLI_DECODER_RESULT_SUCCESS) {
                uncompressedData.resize(decodedSize);
                return true;
            }
            
            return false;
        }

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

    }

}

#endif
