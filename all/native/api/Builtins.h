/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_API_BUILTINS_H_
#define _MASSIF_API_BUILTINS_H_

namespace massif { namespace api {

    /**
     * Registers the SDK's own spec factories and methods, once. Every binding calls it before its
     * first create or call. Defined apart from the registries because it links every implementation;
     * the host tests supply their own definition.
     */
    void registerBuiltins();

} }

#endif
