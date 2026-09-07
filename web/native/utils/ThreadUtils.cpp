#include "utils/ThreadUtils.h"

namespace massif {

    void ThreadUtils::SetThreadPriority(ThreadPriority::ThreadPriority priority) {
        // Web workers have no priority to set - the browser schedules them.
    }

    ThreadUtils::ThreadUtils() {
    }

}
