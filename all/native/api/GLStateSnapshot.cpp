#include "api/GLStateSnapshot.h"

#include <string>

namespace massif { namespace api {

    namespace {
        void compare(std::string& changes, const char* name, int before, int after) {
            if (before != after) {
                changes += (changes.empty() ? "" : ", ") + std::string(name) + " " + std::to_string(before) + "->" + std::to_string(after);
            }
        }

        void compare(std::string& changes, const char* name, const int* before, const int* after, int count) {
            for (int i = 0; i < count; i++) {
                if (before[i] != after[i]) {
                    changes += (changes.empty() ? "" : ", ") + std::string(name) + " [";
                    for (int j = 0; j < count; j++) {
                        changes += (j ? "," : "") + std::to_string(before[j]);
                    }
                    changes += "]->[";
                    for (int j = 0; j < count; j++) {
                        changes += (j ? "," : "") + std::to_string(after[j]);
                    }
                    changes += "]";
                    return;
                }
            }
        }
    }

    std::string GLStateSnapshot::describeChanges(const GLStateSnapshot& before, const GLStateSnapshot& after) {
        std::string changes;
        compare(changes, "program", before.program, after.program);
        compare(changes, "vertex array", before.vertexArray, after.vertexArray);
        compare(changes, "array buffer", before.arrayBuffer, after.arrayBuffer);
        compare(changes, "element array buffer", before.elementArrayBuffer, after.elementArrayBuffer);
        compare(changes, "active texture", before.activeTexture, after.activeTexture);
        compare(changes, "texture 2D", before.texture2D, after.texture2D);
        compare(changes, "framebuffer", before.framebuffer, after.framebuffer);
        compare(changes, "blend", before.blend, after.blend);
        compare(changes, "blend func", before.blendFunc, after.blendFunc, 4);
        compare(changes, "depth test", before.depthTest, after.depthTest);
        compare(changes, "depth mask", before.depthMask, after.depthMask);
        compare(changes, "depth func", before.depthFunc, after.depthFunc);
        compare(changes, "cull face", before.cullFace, after.cullFace);
        compare(changes, "cull face mode", before.cullFaceMode, after.cullFaceMode);
        compare(changes, "scissor test", before.scissorTest, after.scissorTest);
        compare(changes, "scissor box", before.scissorBox, after.scissorBox, 4);
        compare(changes, "viewport", before.viewport, after.viewport, 4);
        return changes;
    }

} }
