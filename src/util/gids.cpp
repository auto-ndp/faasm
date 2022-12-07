#include <faabric/util/gids.h>

#include <atomic>
#include <mutex>
#include <random>

#include <faabric/util/locks.h>
#include <faabric/util/random.h>

static std::atomic<faabric_gid_t> counter = 0;
static std::uint64_t gidKeyHash = 0;
static std::mutex gidMx;

#define GID_LEN 20

namespace faabric::util {
faabric_gid_t generateGid()
{
    if (gidKeyHash == 0) {
        faabric::util::UniqueLock lock(gidMx);
        if (gidKeyHash == 0) {
            // Generate random hash
            std::random_device rd;
            std::uniform_int_distribution<faabric_gid_t> dist;
            gidKeyHash = dist(rd) % (faabric_gid_t(1) << 48);
        }
    }

    faabric_gid_t result = gidKeyHash + counter.fetch_add(1);
    while (result == 0) {
        result = gidKeyHash + counter.fetch_add(1);
    }
    return result & 0x7FFFFFFF;
}
}
