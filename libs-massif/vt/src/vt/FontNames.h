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
     * Splits a CSS-like font list ("Roboto, Helvetica Neue, sans-serif") into its names, most preferred first,
     * unquoted and trimmed. Entries tagged for another platform ("android:Roboto", "ios:...") are dropped.
     */
    std::vector<std::string> parseFontNames(const std::string& names);

    /**
     * Whether a face of a font collection (.ttc) is the one a name asks for: "Helvetica Neue Bold"
     * is the face named HelveticaNeue-Bold, not face 0. Case, spaces, dashes and a query are ignored.
     */
    bool fontFaceMatches(const std::string& requested, const std::string& familyName, const std::string& styleName, const std::string& postScriptName);
}

#endif
