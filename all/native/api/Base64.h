#ifndef _MASSIF_API_BASE64_H_
#define _MASSIF_API_BASE64_H_

#include <string>
#include <vector>

namespace massif { namespace api {

    /**
     * Decodes standard base64, padded or not; whitespace is skipped.
     * @return False on any other character or a dangling single sextet, leaving `out` empty.
     */
    inline bool decodeBase64(const std::string& text, std::vector<unsigned char>& out) {
        out.clear();
        unsigned int buffer = 0;
        int bits = 0;
        size_t sextets = 0;
        for (char c : text) {
            int value;
            if (c >= 'A' && c <= 'Z') {
                value = c - 'A';
            } else if (c >= 'a' && c <= 'z') {
                value = c - 'a' + 26;
            } else if (c >= '0' && c <= '9') {
                value = c - '0' + 52;
            } else if (c == '+') {
                value = 62;
            } else if (c == '/') {
                value = 63;
            } else if (c == '=' || c == '\n' || c == '\r' || c == ' ') {
                continue;
            } else {
                out.clear();
                return false;
            }
            sextets++;
            buffer = (buffer << 6) | static_cast<unsigned int>(value);
            bits += 6;
            if (bits >= 8) {
                bits -= 8;
                out.push_back(static_cast<unsigned char>((buffer >> bits) & 0xFF));
            }
        }
        if (sextets % 4 == 1) {
            out.clear();
            return false;
        }
        return true;
    }

} }

#endif
