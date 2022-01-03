#pragma once

#include <atomic>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <thread>
#include <utility>

#include <faabric/util/locks.h>
#include <faabric/util/memory.h>

#include <absl/container/btree_set.h>
#include <boost/container/flat_map.hpp>
#include <boost/container/small_vector.hpp>

#include <fcntl.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace uffd {

struct UffdMemoryRange : public std::enable_shared_from_this<UffdMemoryRange>
{
    std::byte* mapStart = nullptr;
    size_t mapBytes = 0;
    std::atomic<size_t> maxUsedBytes = 0;
    // map of PROT_R/W/X permissions
    // key = starting address
    // value = permissions until next address on the list
    using PermissionList = boost::container::flat_map<
      uintptr_t,
      int,
      std::less<uintptr_t>,
      boost::container::small_vector<std::pair<uintptr_t, int>, 8>>;
    PermissionList permissions;
    std::shared_mutex mx;
    std::span<std::byte> initSource;

    UffdMemoryRange() = delete;
    ~UffdMemoryRange() noexcept;
    explicit UffdMemoryRange(size_t pages, size_t alignmentLog2 = 0);
    UffdMemoryRange(const UffdMemoryRange&) = delete;
    UffdMemoryRange& operator=(const UffdMemoryRange&) = delete;
    inline UffdMemoryRange(UffdMemoryRange&& rhs) noexcept
    {
        *this = std::move(rhs);
    }
    inline UffdMemoryRange& operator=(UffdMemoryRange&& rhs) noexcept
    {
        this->~UffdMemoryRange();
        this->mapStart = rhs.mapStart;
        this->mapBytes = rhs.mapBytes;
        this->permissions = std::move(rhs.permissions);
        rhs.permissions.clear();
        rhs.permissions.emplace(0, 0);
        rhs.permissions.emplace(UINTPTR_MAX, 0);
        rhs.mapStart = nullptr;
        rhs.mapBytes = 0;
        return *this;
    }

    inline void resetPermissions()
    {
        permissions = { { 0, 0 }, { UINTPTR_MAX, 0 } };
    }

    inline void discardContent(const std::byte* begin, const std::byte* end)
    {
        if (begin == end) {
            return;
        }
        if (end < begin || begin <= mapStart || end > (mapStart + mapBytes)) {
            throw std::out_of_range("Discard UFFD range out of bounds");
        }
        madvise((void*)begin, end - begin, MADV_DONTNEED);
    }

    inline void touch(const size_t offset)
    {
        if (maxUsedBytes < offset) {
            maxUsedBytes = offset;
        }
    }

    inline void discardAll()
    {
        madvise((void*)mapStart, maxUsedBytes, MADV_DONTNEED);
        maxUsedBytes = 0;
    }

    inline void setPermissions(const std::byte* begin,
                               const std::byte* end,
                               int perms)
    {
        const uintptr_t beginA = (uintptr_t)begin;
        const uintptr_t endA = (uintptr_t)end;
        const uintptr_t lastTouchedByte = end - mapStart;
        if (lastTouchedByte > maxUsedBytes) {
            maxUsedBytes = lastTouchedByte;
        }
        PermissionList oldPerms;
        oldPerms.reserve(permissions.size() + 1);
        oldPerms.swap(permissions);
        int lastPerm = -1;
        int endPerm = oldPerms.begin()->second;
        bool needsEnd = true;
        bool beginDone = false;
        for (const auto& [oldAddr, oldPerm] : oldPerms) {
            if (!beginDone && oldAddr >= beginA) {
                beginDone = true;
                if (lastPerm != perms) {
                    permissions.emplace(beginA, perms);
                }
            }
            if (oldAddr < beginA || oldAddr >= endA) {
                if (oldPerm != lastPerm || oldAddr == UINTPTR_MAX) {
                    permissions.emplace(oldAddr, oldPerm);
                }
                if (oldAddr == endA) {
                    needsEnd = false;
                }
            } else {
                endPerm = oldPerm;
            }
            lastPerm = oldPerm;
        }
        if (needsEnd) {
            permissions.emplace(endA, endPerm);
        }
    }

    inline bool pointerInRange(const std::byte* ptr) const
    {
        size_t addr = (size_t)ptr;
        return addr >= (size_t)mapStart && addr < (size_t)(mapStart + mapBytes);
    }

    inline int pointerPermissions(const std::byte* ptr) const
    {
        size_t addr = (size_t)ptr;
        auto it = permissions.upper_bound(addr);
        if (it == permissions.begin() || it == permissions.end()) {
            return 0;
        }
        --it;
        return it->second;
    }

    inline std::strong_ordering operator<=>(const UffdMemoryRange& rhs) const
    {
        return size_t(mapStart) <=> size_t(rhs.mapStart);
    }

    inline friend std::strong_ordering operator<=>(const UffdMemoryRange& umr,
                                                   const std::byte* ptr)
    {
        if (umr.pointerInRange(ptr)) {
            return std::strong_ordering::equal;
        }
        return size_t(umr.mapStart) <=> size_t(ptr);
    }

    inline friend std::strong_ordering operator<=>(const std::byte* ptr,
                                                   const UffdMemoryRange& umr)
    {
        return umr <=> ptr;
    }
};

struct UffdMemoryRangeLess final
{
    using is_transparent = std::true_type;

    bool operator()(const UffdMemoryRange& a1, const std::byte* a2) const
    {
        return a1 < a2;
    }

    bool operator()(const std::byte* a1, const UffdMemoryRange& a2) const
    {
        return a1 < a2;
    }

    bool operator()(const UffdMemoryRange& a1, const UffdMemoryRange& a2) const
    {
        return a1 < a2;
    }

    bool operator()(const std::shared_ptr<UffdMemoryRange>& a1,
                    const std::shared_ptr<UffdMemoryRange>& a2) const
    {
        return (*a1) < (*a2);
    }

    bool operator()(const std::shared_ptr<UffdMemoryRange>& a1,
                    const std::byte* a2) const
    {
        return (*a1) < a2;
    }

    bool operator()(const std::byte* a1,
                    const std::shared_ptr<UffdMemoryRange>& a2) const
    {
        return a1 < (*a2);
    }
};

class UffdMemoryArenaManager final
{
  public:
    UffdMemoryArenaManager(const UffdMemoryArenaManager&) = delete;
    UffdMemoryArenaManager& operator=(const UffdMemoryArenaManager&) = delete;
    static UffdMemoryArenaManager& instance();

    // Allocates a UFFD-backed memory range, and returns the pointer to the
    // beginning of it.
    std::byte* allocateRange(size_t pages, size_t alignmentLog2 = 0);

    void modifyRange(std::byte* start,
                     std::function<void(UffdMemoryRange&)> action);

    // Remove and free a UFFD-backed range completely
    void freeRange(std::byte* start);

    // The signal handler
    friend void sigbusHandler(int code, siginfo_t* siginfo, void* contextR);

    using RangeSet =
      absl::btree_set<std::shared_ptr<UffdMemoryRange>, UffdMemoryRangeLess>;

    std::shared_ptr<RangeSet> rangesSnapshot() const
    {
        return std::atomic_load_explicit(&ranges, std::memory_order_acquire);
    }

  private:
    TraceableSharedMutex(mx);
    faabric::util::UserfaultFd uffd;
    std::shared_ptr<RangeSet> ranges;

    // Needs to be called while holding an exclusive lock over mx
    // Don't hold a reference to old ranges or a deadlock will happen
    void updateRanges(std::shared_ptr<RangeSet> newRanges)
    {
        std::shared_ptr oldRanges = std::atomic_exchange_explicit(
          &ranges, newRanges, std::memory_order_acq_rel);
        // Make it most likely for this thread to destroy the object rather than
        // a reader
        while (oldRanges.use_count() > 1) {
            std::this_thread::yield();
        }
    }

    UffdMemoryArenaManager();
    ~UffdMemoryArenaManager() = default;
};

}
