#include "network/HTTPClientEmscriptenImpl.h"
#include "components/Exceptions.h"
#include "utils/Log.h"

#include <cstdlib>
#include <cstring>
#include <vector>

#include <emscripten/em_js.h>

namespace massif {

    namespace {
        // After a synchronous XHR this worker's view of the heap is stale until V8 handles an interrupt, and a copy
        // through it threw "offset is out of bounds": grow(0) refreshes it (docs/maintenance/web-build.md).
        EM_JS(int, massif_xhr_send, (const char* method, const char* url, const char** headers, const unsigned char* body, int bodyLength, int timeout), {
            wasmMemory.grow(0);
            growMemViews();
            var xhr = new XMLHttpRequest();
            xhr.open(UTF8ToString(method), UTF8ToString(url), false);
            xhr.responseType = 'arraybuffer';
            if (timeout > 0) {
                xhr.timeout = timeout;
            }
            for (var i = 0; ; i += 2) {
                var key = HEAPU32[(headers >> 2) + i];
                if (!key) {
                    break;
                }
                xhr.setRequestHeader(UTF8ToString(key), UTF8ToString(HEAPU32[(headers >> 2) + i + 1]));
            }
            var data = (bodyLength > 0 ? HEAPU8.slice(body, body + bodyLength) : null);
            try {
                xhr.send(data);
            } catch (e) {
            }
            wasmMemory.grow(0);
            globalThis.__massifXHRs ??= [];
            globalThis.__massifXHRs.push(xhr);
            return globalThis.__massifXHRs.length - 1;
        });

        EM_JS(int, massif_xhr_status, (int handle), {
            return globalThis.__massifXHRs[handle].status;
        });

        EM_JS(char*, massif_xhr_headers, (int handle), {
            return stringToNewUTF8(globalThis.__massifXHRs[handle].getAllResponseHeaders() || '');
        });

        EM_JS(int, massif_xhr_length, (int handle), {
            var response = globalThis.__massifXHRs[handle].response;
            return response ? response.byteLength : 0;
        });

        EM_JS(void, massif_xhr_copy, (int handle, unsigned char* data), {
            wasmMemory.grow(0);
            growMemViews();
            HEAPU8.set(new Uint8Array(globalThis.__massifXHRs[handle].response), data);
        });

        EM_JS(void, massif_xhr_free, (int handle), {
            globalThis.__massifXHRs[handle] = null;
            while (globalThis.__massifXHRs.length > 0 && !globalThis.__massifXHRs[globalThis.__massifXHRs.length - 1]) {
                globalThis.__massifXHRs.pop();
            }
        });

        std::string trimHeader(const std::string& text) {
            std::size_t begin = text.find_first_not_of(" \t\r\n");
            if (begin == std::string::npos) {
                return std::string();
            }
            return text.substr(begin, text.find_last_not_of(" \t\r\n") - begin + 1);
        }
    }

    HTTPClient::EmscriptenImpl::EmscriptenImpl(bool log) :
        _log(log),
        _timeout(-1)
    {
    }

    void HTTPClient::EmscriptenImpl::setTimeout(int milliseconds) {
        _timeout = milliseconds;
    }

    bool HTTPClient::EmscriptenImpl::makeRequest(const HTTPClient::Request& request, HeadersFunc headersFn, DataFunc dataFn) const {
        // Flat key, value, ..., null array; the strings must outlive the call.
        std::vector<std::string> headerStrings;
        for (auto it = request.headers.begin(); it != request.headers.end(); it++) {
            headerStrings.push_back(it->first);
            headerStrings.push_back(it->second);
        }
        if (!request.contentType.empty()) {
            headerStrings.push_back("Content-Type");
            headerStrings.push_back(request.contentType);
        }
        std::vector<const char*> headerPtrs;
        for (const std::string& headerString : headerStrings) {
            headerPtrs.push_back(headerString.c_str());
        }
        headerPtrs.push_back(nullptr);

        int handle = massif_xhr_send(request.method.c_str(), request.url.c_str(), headerPtrs.data(), request.body.data(), static_cast<int>(request.body.size()), _timeout.load());
        std::shared_ptr<void> xhrGuard(nullptr, [handle](void*) { massif_xhr_free(handle); });

        // status 0 is what a failed CORS preflight or a network error looks like from here; the
        // browser keeps the reason to itself and only logs it to the console.
        int status = massif_xhr_status(handle);
        if (status == 0) {
            throw NetworkException("Unable to receive response", request.url);
        }

        std::map<std::string, std::string> headers;
        if (char* headersText = massif_xhr_headers(handle)) {
            std::string text = headersText;
            std::free(headersText);
            std::size_t lineStart = 0;
            while (lineStart < text.size()) {
                std::size_t lineEnd = text.find("\r\n", lineStart);
                std::string line = text.substr(lineStart, lineEnd == std::string::npos ? std::string::npos : lineEnd - lineStart);
                std::size_t colon = line.find(':');
                if (colon != std::string::npos) {
                    headers[trimHeader(line.substr(0, colon))] = trimHeader(line.substr(colon + 1));
                }
                if (lineEnd == std::string::npos) {
                    break;
                }
                lineStart = lineEnd + 2;
            }
        }

        if (_log) {
            Log::Infof("HTTPClient::EmscriptenImpl::makeRequest: Response %d for %s", status, request.url.c_str());
        }

        if (!headersFn(status, headers)) {
            return false;
        }
        int length = massif_xhr_length(handle);
        if (length > 0) {
            std::vector<unsigned char> data(static_cast<std::size_t>(length));
            massif_xhr_copy(handle, data.data());
            if (!dataFn(data.data(), data.size())) {
                return false;
            }
        }
        return true;
    }

}
