#pragma once

#include <faabric/util/logging.h>
#include <faabric/util/timing.h>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <shared_mutex>

#define DEFAULT_FLAG_WAIT_MS 10000

#define TraceableMutex(varname) TracyLockable(std::mutex, varname)
#define TraceableSharedMutex(varname)                                          \
    TracySharedLockable(std::shared_mutex, varname)

namespace faabric::util {
typedef std::unique_lock<std::mutex> UniqueLock;
typedef std::unique_lock<LockableBase(std::mutex)> UniqueTraceableLock;
typedef std::unique_lock<std::shared_mutex> FullLock;
typedef std::unique_lock<SharedLockableBase(std::shared_mutex)>
  FullTraceableLock;
typedef std::shared_lock<std::shared_mutex> SharedLock;
typedef std::shared_lock<SharedLockableBase(std::shared_mutex)>
  SharedTraceableLock;

class FlagWaiter : public std::enable_shared_from_this<FlagWaiter>
{
  public:
    FlagWaiter(int timeoutMsIn = DEFAULT_FLAG_WAIT_MS);

    void waitOnFlag();

    void setFlag(bool value);

  private:
    int timeoutMs;

    std::mutex flagMx;
    std::condition_variable cv;
    std::atomic<bool> flag;
};
}
