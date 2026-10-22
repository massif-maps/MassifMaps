// The platform halves the cache's download thread pool links against (android/ and ios/ define
// them); nothing here starts a thread, so a body that only runs the task is enough.

#include "components/Task.h"
#include "utils/ThreadUtils.h"

namespace massif {

    void Task::operator()() {
        run();
    }

    void ThreadUtils::SetThreadPriority(ThreadPriority::ThreadPriority) {
    }

}
