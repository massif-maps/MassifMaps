/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_API_ELEMENTSPECS_H_
#define _MASSIF_API_ELEMENTSPECS_H_

namespace massif { namespace api {

    /**
     * The "element" and "elementstyle" kinds. Their own translation unit because a style is the one
     * spec that needs a builder rather than a constructor.
     */
    void registerElementFactories();

} }

#endif
