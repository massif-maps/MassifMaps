#include "network/HTTPClientEmscriptenImpl.h"
#include "components/Exceptions.h"
#include "utils/Log.h"

#include <cstring>
#include <vector>

#include <emscripten/fetch.h>

namespace massif {

    namespace {
        // emscripten_fetch_unpack_response_headers keeps the space after the colon and copies one
        // character too many, so every value arrives as " 1234\n" - which is not what a
        // Content-Length parses from.
        std::string trimHeader(const char* text) {
            std::string value = text;
            std::size_t begin = value.find_first_not_of(" \t\r\n");
            if (begin == std::string::npos) {
                return std::string();
            }
            return value.substr(begin, value.find_last_not_of(" \t\r\n") - begin + 1);
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
        emscripten_fetch_attr_t attr;
        emscripten_fetch_attr_init(&attr);
        std::strncpy(attr.requestMethod, request.method.c_str(), sizeof(attr.requestMethod) - 1);
        attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY | EMSCRIPTEN_FETCH_SYNCHRONOUS;
        int timeout = _timeout.load();
        if (timeout > 0) {
            attr.timeoutMSecs = static_cast<std::uint32_t>(timeout);
        }

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
        attr.requestHeaders = headerPtrs.data();

        if (!request.body.empty()) {
            attr.requestData = reinterpret_cast<const char*>(request.body.data());
            attr.requestDataSize = request.body.size();
        }

        emscripten_fetch_t* fetch = emscripten_fetch(&attr, request.url.c_str());
        if (!fetch) {
            throw NetworkException("Unable to open connection", request.url);
        }
        std::shared_ptr<emscripten_fetch_t> fetchGuard(fetch, emscripten_fetch_close);

        // status 0 is what a failed CORS preflight or a network error looks like from here; the
        // browser keeps the reason to itself and only logs it to the console.
        if (fetch->status == 0) {
            throw NetworkException("Unable to receive response", request.url);
        }

        std::map<std::string, std::string> headers;
        if (std::size_t headersLength = emscripten_fetch_get_response_headers_length(fetch)) {
            std::vector<char> headersString(headersLength + 1, '\0');
            emscripten_fetch_get_response_headers(fetch, headersString.data(), headersString.size());
            if (char** unpacked = emscripten_fetch_unpack_response_headers(headersString.data())) {
                for (int i = 0; unpacked[i] && unpacked[i + 1]; i += 2) {
                    headers[trimHeader(unpacked[i])] = trimHeader(unpacked[i + 1]);
                }
                emscripten_fetch_free_unpacked_response_headers(unpacked);
            }
        }

        if (_log) {
            Log::Infof("HTTPClient::EmscriptenImpl::makeRequest: Response %d for %s", static_cast<int>(fetch->status), request.url.c_str());
        }

        if (!headersFn(fetch->status, headers)) {
            return false;
        }
        if (fetch->numBytes > 0 && fetch->data) {
            if (!dataFn(reinterpret_cast<const unsigned char*>(fetch->data), static_cast<std::size_t>(fetch->numBytes))) {
                return false;
            }
        }
        return true;
    }

}
