/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_API_DOWNLOADMETHODS_H_
#define _MASSIF_API_DOWNLOADMETHODS_H_

#ifdef _MASSIF_OFFLINE_SUPPORT

namespace massif { namespace api {

    /**
     * Offline area downloads: startDownloadArea/stopAllDownloads plus the download.* events.
     *
     * Its own TU because it pulls in the persistent cache, and a reduced property table has to link
     * without it.
     */
    void registerDownloadMethods();

} }

#endif

#endif
