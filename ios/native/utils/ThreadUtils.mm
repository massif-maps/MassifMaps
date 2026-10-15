#include "utils/ThreadUtils.h"
#include "components/ThreadWorker.h"
#include "utils/Log.h"

#include <algorithm>

#include <sys/syscall.h>
#include <TargetConditionals.h>

#import <Foundation/NSThread.h>

namespace massif {

    void ThreadUtils::SetThreadPriority(ThreadPriority::ThreadPriority priority) {
        NSThread* nsThread = [NSThread currentThread];
        double nsPriority = 1 - static_cast<double>(static_cast<int>(priority) - static_cast<int>(ThreadPriority::MAXIMUM)) / (static_cast<int>(ThreadPriority::MINIMUM) - static_cast<int>(ThreadPriority::MAXIMUM));
#if TARGET_OS_MACCATALYST || TARGET_OS_OSX
        // 0.0 is the background band: a pool of 4+ threads runs ~2-4x slower there than at 0.1
        nsPriority = std::max(nsPriority, 0.1);
#endif
        [nsThread setThreadPriority:nsPriority];
    }
    
    ThreadUtils::ThreadUtils() {
    }

}
