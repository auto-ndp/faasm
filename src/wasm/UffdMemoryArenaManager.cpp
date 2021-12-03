#include "UffdMemoryArenaManager.h"

#include <faabric/util/timing.h>

#include <stdexcept>
#include <sys/mman.h>

namespace uffd {

UffdMemoryRange::~UffdMemoryRange() noexcept
{
    if (this->mapStart != nullptr && this->mapBytes > 0) {
        if (munmap(mapStart, mapBytes) < 0) {
            perror("Warning: error unmapping UffdMemoryRange");
        }
        this->mapStart = nullptr;
        this->mapBytes = 0;
        this->validBytes = 0;
    }
}
UffdMemoryRange::UffdMemoryRange(size_t pages, size_t alignmentLog2)
{
    const size_t numBytes = pages * 4096;
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
        if (munmap(unalignedBaseAddress, numHeadPaddingBytes) < 0) {
            throw std::system_error(errno,
                                    std::generic_category(),
                                    "Can't munmap unaligned uffd memory range");
        }
    }

    const size_t numTailPaddingBytes =
      alignmentBytes - (alignedAddress - address);
    if (numTailPaddingBytes > 0) {
        if (munmap(alignedPtr + numBytes, numTailPaddingBytes) < 0) {
            throw std::system_error(errno,
                                    std::generic_category(),
                                    "Can't munmap unaligned uffd memory range");
        }
    }

    madvise(alignedPtr, numBytes, MADV_HUGEPAGE);
    TracyAllocNS(alignedPtr, numBytes, 6, "UFFD");

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
        madvise((void*)(range->mapStart + newValidBytes),
                oldValidBytes - newValidBytes,
                MADV_DONTNEED);
    }
}

}
