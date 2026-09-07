/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_VT_FONTNAMES_H_
#define _MASSIF_VT_FONTNAMES_H_

#include <string>
#include <vector>

namespace massif::vt {
    /**
     * Splits a CSS-like font list ("Roboto, Helvetica Neue, sans-serif") into its names, the most
     * preferred first. An entry may name the platform it is meant for ("android:Roboto",
     * "ios:Helvetica Neue"); an entry tagged for another platform is dropped. Surrounding quotes
     * and whitespace are removed. A single name without a comma simply comes back as one entry.
     */
    std::vector<std::string> parseFontNames(const std::string& names);
}

#endif
