/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_HTTPCLIENTEMSCRIPTENIMPL_H_
#define _MASSIF_HTTPCLIENTEMSCRIPTENIMPL_H_

#include "network/HTTPClient.h"

#include <atomic>

namespace massif {

    /**
     * The Fetch API, called synchronously.
     *
     * Every caller in the SDK blocks on the response, so this matches the other platforms - but a
     * synchronous fetch is only legal off the browser's main thread, which is why the web build
     * runs the SDK on a worker (PROXY_TO_PTHREAD). The whole body arrives at once, so a streaming
     * request hands its data to the callback in a single call.
     */
    class HTTPClient::EmscriptenImpl : public HTTPClient::Impl {
    public:
        explicit EmscriptenImpl(bool log);

        virtual void setTimeout(int milliseconds);
        virtual bool makeRequest(const HTTPClient::Request& request, HeadersFunc headersFn, DataFunc dataFn) const;

    private:
        const bool _log;
        std::atomic<int> _timeout;
    };

}

#endif
