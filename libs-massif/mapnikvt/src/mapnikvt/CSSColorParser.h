/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_MAPNIKVT_CSSCOLORPARSER_H_
#define _MASSIF_MAPNIKVT_CSSCOLORPARSER_H_

#include <string>

namespace massif::mvt {
    bool parseCSSColor(std::string name, unsigned int& value);
}

#endif
