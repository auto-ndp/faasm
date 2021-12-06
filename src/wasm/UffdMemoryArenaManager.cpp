#include "UffdMemoryArenaManager.h"

#include <faabric/util/timing.h>

#include <stdexcept>
#include <sys/mman.h>

namespace uffd {

void check_errno(int ec, const char* msg)
{
    if (ec < 0) {
        perror(msg);
        throw std::system_error(errno, std::generic_category(), msg);
    }
}

UffdMemoryRange::~UffdMemoryRange() noexcept
{
    if (this->mapStart != nullptr && this->mapBytes > 0) {
        ZoneScopedN("UffdMemoryRange::munmap");
        TracyFreeNS(this->mapStart, 6, "UFFD-Virtual");
        check_errno(munmap(mapStart, mapBytes),
                    "Warning: error unmapping UffdMemoryRange");
        this->mapStart = nullptr;
        this->mapBytes = 0;
        this->validBytes = 0;
    }
}
UffdMemoryRange::UffdMemoryRange(size_t pages, size_t alignmentLog2)
{
    ZoneScopedN("UffdMemoryRange::mmap");
    const size_t numBytes = pages * 4096;
    ZoneValue(numBytes);
    const size_t alignmentBytes = 1ULL << alignmentLog2;
    void* unalignedBaseAddress = mmap(nullptr,
                                      numBytes + alignmentBytes,
                                      PROT_READ | PROT_WRITE,
                                      MAP_PRIVATE | MAP_ANONYMOUS,
                                      -1,
                                      0);
    if (unalignedBaseAddress == MAP_FAILED) {
        throw std::system_error(
          errno, std::generic_category(), "Can't mmap uffd memory range");
    }

    const size_t address = reinterpret_cast<size_t>(unalignedBaseAddress);
    const size_t alignedAddress =
      (address + alignmentBytes - 1) & ~(alignmentBytes - 1);
    std::byte* alignedPtr = reinterpret_cast<std::byte*>(alignedAddress);

    const size_t numHeadPaddingBytes = alignedAddress - address;
    if (numHeadPaddingBytes > 0) {
        check_errno(munmap(unalignedBaseAddress, numHeadPaddingBytes),
                    "Can't munmap unaligned uffd memory range");
    }

    const size_t numTailPaddingBytes =
      alignmentBytes - (alignedAddress - address);
    if (numTailPaddingBytes > 0) {
        check_errno(munmap(alignedPtr + numBytes, numTailPaddingBytes),
                    "Can't munmap unaligned uffd memory range");
    }

    check_errno(madvise(alignedPtr, numBytes, MADV_HUGEPAGE), "MADV_HUGEPAGE");
    TracyAllocNS(alignedPtr, numBytes, 6, "UFFD-Virtual");

    this->mapStart = alignedPtr;
    this->mapBytes = numBytes;
    this->validBytes = 0;
}

UffdMemoryArenaManager& UffdMemoryArenaManager::instance()
{
    static UffdMemoryArenaManager umam;
    return umam;
}

UffdMemoryArenaManager::UffdMemoryArenaManager()
{
    faabric::util::FullLock lock{ mx };
    uffd.create(O_CLOEXEC, true);
}

std::byte* UffdMemoryArenaManager::allocateRange(size_t pages,
                                                 size_t alignmentLog2)
{
    UffdMemoryRange range{ pages, alignmentLog2 };
    std::byte* start = range.mapStart;
    {
        faabric::util::FullLock lock{ mx };
        this->uffd.registerAddressRange(
          size_t(range.mapStart), range.mapBytes, true, false);
        this->ranges.insert(std::move(range));
    }
    return start;
}

void UffdMemoryArenaManager::validateAndResizeRange(std::byte* oldEnd,
                                                    std::byte* newEnd)
{
    faabric::util::SharedLock lock{ mx };
    auto range = this->ranges.find(oldEnd);
    if (range == this->ranges.end()) {
        throw std::runtime_error("Invalid pointer for UFFD range resize");
    }
    const std::byte* actualOldEnd =
      range->mapStart + range->validBytes.load(std::memory_order_acquire);
    if (actualOldEnd != oldEnd) {
        throw std::runtime_error("Mismatched UFFD range resize end pointers");
    }
    if (!range->pointerInRange(newEnd)) {
        throw std::runtime_error("New UFFD range end out of allocated range");
    }
    const size_t newPages = (newEnd - range->mapStart) / 4096;
    resizeRangeImpl(range, newPages);
}

void UffdMemoryArenaManager::discardRange(std::byte* start)
{
    faabric::util::SharedLock lock{ mx };
    auto range = this->ranges.find(start);
    if (range == this->ranges.end()) {
        throw std::runtime_error("Invalid pointer for UFFD range discard");
    }
    check_errno(madvise((void*)(range->mapStart),
                        range->validBytes.load(std::memory_order_acquire),
                        MADV_DONTNEED),
                "MADV_DONTNEED");
}

void UffdMemoryArenaManager::freeRange(std::byte* start)
{
    faabric::util::FullLock lock{ mx };
    auto range = this->ranges.find(start);
    if (range == this->ranges.end()) {
        throw std::runtime_error("Invalid pointer for UFFD range removal");
    }
    size_t mapStart = size_t(range->mapStart);
    size_t mapLength = range->mapBytes;
    uffd.unregisterAddressRange(mapStart, mapLength);
    this->ranges.erase(range);
}

void UffdMemoryArenaManager::resizeRangeImpl(
  UffdMemoryArenaManager::RangeSet::iterator range,
  size_t newPages)
{
    const size_t newValidBytes = newPages * 4096;
    const size_t oldValidBytes =
      range->validBytes.load(std::memory_order_acquire);
    if (newValidBytes == oldValidBytes) {
        return;
    }
    range->validBytes.store(newValidBytes, std::memory_order_release);
    if (newValidBytes < oldValidBytes) {
        check_errno(madvise((void*)(range->mapStart + newValidBytes),
                            oldValidBytes - newValidBytes,
                            MADV_DONTNEED),
                    "MADV_DONTNEED");
    }
}

}
