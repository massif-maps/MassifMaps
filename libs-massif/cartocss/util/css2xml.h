/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_CSSUTILS_CSS2XML_H_
#define _MASSIF_CSSUTILS_CSS2XML_H_

#include <string>
#include <vector>

namespace massif::cssutils {
    /**
     * Compiles a CartoCSS style project to the mapnik XML the decoder reads.
     * args: [--roundtrip] input-project-file output-xml-file. Returns a process exit code.
     */
    int css2xmlMain(const std::vector<std::string>& args);
}

#endif
