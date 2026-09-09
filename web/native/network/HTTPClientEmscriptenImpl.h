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
     * The Fetch API, called synchronously, which is what every caller in the SDK expects. Legal
     * only off the browser's main thread - the SDK's own tile and envelope pools are threads, so
     * that holds. The whole body arrives at once, so a streaming request gets one callback.
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
