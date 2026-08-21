#include "api/Builtins.h"
#include "api/Methods.h"
#include "api/Spec.h"

namespace massif { namespace api {

    void registerBuiltins() {
        // A function-local static's initialiser runs exactly once, and other threads block on it,
        // so two bindings racing to their first create cannot double-register.
        static bool registered = (Spec::registerBuiltinFactories(), Methods::registerBuiltins(), true);
        (void)registered;
    }

} }
