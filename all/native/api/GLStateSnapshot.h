/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_API_GLSTATESNAPSHOT_H_
#define _MASSIF_API_GLSTATESNAPSHOT_H_

#include <string>

namespace massif { namespace api {

    /** The GL state a custom layer's render must leave as it found it (see mm_custom_layer). */
    struct GLStateSnapshot {
        int program = 0;
        int vertexArray = 0;
        int arrayBuffer = 0;
        int elementArrayBuffer = 0;
        int activeTexture = 0;
        int texture2D = 0;
        int framebuffer = 0;
        int blend = 0;
        int blendFunc[4] = { 0, 0, 0, 0 };
        int depthTest = 0;
        int depthMask = 0;
        int depthFunc = 0;
        int cullFace = 0;
        int cullFaceMode = 0;
        int scissorTest = 0;
        int scissorBox[4] = { 0, 0, 0, 0 };
        int viewport[4] = { 0, 0, 0, 0 };

        /** What differs, as "name before->after" separated by commas; empty when nothing does. */
        static std::string describeChanges(const GLStateSnapshot& before, const GLStateSnapshot& after);
    };

} }

#endif
