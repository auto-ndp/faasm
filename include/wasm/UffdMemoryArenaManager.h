#pragma once

#include <atomic>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>

#include <faabric/util/locks.h>
#include <faabric/util/memory.h>

#include <absl/container/btree_set.h>

namespace uffd {

struct UffdMemoryRange
{
    std::byte* mapStart = nullptr;
    size_t mapBytes = 0;
    std::atomic<size_t> validBytes = 0;
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
        this->validBytes.store(rhs.validBytes.load(std::memory_order_acquire),
                               std::memory_order_release);
        rhs.mapStart = nullptr;
        rhs.mapBytes = 0;
        rhs.validBytes = 0;
        return *this;
    }

    inline bool pointerInRange(const std::byte* ptr) const
    {
        size_t addr = (size_t)ptr;
        return addr >= (size_t)mapStart && addr < (size_t)(mapStart + mapBytes);
    }

    inline bool pointerInValidRange(const std::byte* ptr) const
    {
        size_t addr = (size_t)ptr;
        return addr >= (size_t)mapStart &&
               addr < (size_t)(mapStart +
                               validBytes.load(std::memory_order_acquire));
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

class UffdMemoryArenaManager final
{
  public:
    UffdMemoryArenaManager(const UffdMemoryArenaManager&) = delete;
    UffdMemoryArenaManager& operator=(const UffdMemoryArenaManager&) = delete;
    static UffdMemoryArenaManager& instance();

    // Allocates a UFFD-backed memory range, and returns the pointer to the
    // beginning of it.
    std::byte* allocateRange(size_t pages, size_t alignmentLog2 = 0);

    void validateAndResizeRange(std::byte* oldEnd, std::byte* newEnd);

    // Free all range memory, and set it to be valid with the underlying
    // snapshot
    void discardRange(std::byte* start);

    // Remove and free a UFFD-backed range completely
    void freeRange(std::byte* start);

  private:
    std::shared_mutex mx;
    faabric::util::UserfaultFd uffd;
    using RangeSet = absl::btree_set<UffdMemoryRange, std::less<>>;
    RangeSet ranges;

    void resizeRangeImpl(RangeSet::iterator range, size_t newPages);

    UffdMemoryArenaManager();
    ~UffdMemoryArenaManager() = default;
};

}
