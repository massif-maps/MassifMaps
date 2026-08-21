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
     * first create or call.
     *
     * Declared here and defined in Builtins.cpp so that the registries themselves depend on
     * nothing: the definition pulls in every source, layer and method implementation, which is
     * exactly what the host tests cannot link. They define their own instead - a separate program,
     * so a second definition is not a violation, and it is what lets create() and call() be tested
     * over a handful of classes.
     */
    void registerBuiltins();

} }

#endif
