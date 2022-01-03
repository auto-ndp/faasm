#include "UffdMemoryArenaManager.h"

#include <faabric/util/crash.h>
#include <faabric/util/files.h>
#include <faabric/util/timing.h>

#include <stdexcept>
#include <sys/mman.h>

namespace uffd {

using faabric::util::checkErrno;

UffdMemoryRange::~UffdMemoryRange() noexcept
{
    if (this->mapStart != nullptr && this->mapBytes > 0) {
        ZoneScopedN("UffdMemoryRange::munmap");
        TracyFreeNS(this->mapStart, 6, "UFFD-Virtual");
        checkErrno(munmap(mapStart, mapBytes),
                   "Warning: error unmapping UffdMemoryRange");
        this->mapStart = nullptr;
        this->mapBytes = 0;
        resetPermissions();
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
        checkErrno(munmap(unalignedBaseAddress, numHeadPaddingBytes),
                   "Can't munmap unaligned uffd memory range");
    }

    const size_t numTailPaddingBytes =
      alignmentBytes - (alignedAddress - address);
    if (numTailPaddingBytes > 0) {
        checkErrno(munmap(alignedPtr + numBytes, numTailPaddingBytes),
                   "Can't munmap unaligned uffd memory range");
    }

    checkErrno(madvise(alignedPtr, numBytes, MADV_HUGEPAGE), "MADV_HUGEPAGE");
    TracyAllocNS(alignedPtr, numBytes, 6, "UFFD-Virtual");

    this->mapStart = alignedPtr;
    this->mapBytes = numBytes;
    resetPermissions();
}

UffdMemoryArenaManager& UffdMemoryArenaManager::instance()
{
    static UffdMemoryArenaManager umam;
    return umam;
}

void sigbusHandler(int code, siginfo_t* siginfo, void* contextR)
{
    ZoneScopedN("uffd-fh");
    constexpr size_t FAULT_PAGE_SIZE = 65536;
    if (code != SIGBUS) [[unlikely]] {
        std::terminate();
    }
    std::byte* faultAddr = (std::byte*)siginfo->si_addr;
    thread_local std::weak_ptr<UffdMemoryRange> rangeCache;
    std::shared_ptr range = rangeCache.lock();
    UffdMemoryArenaManager& umam = UffdMemoryArenaManager::instance();
    if (!range || !range->pointerInRange(faultAddr)) [[unlikely]] {
        auto ranges = umam.rangesSnapshot();
        auto rangeIt = ranges->find(faultAddr);
        if (rangeIt == ranges->end()) [[unlikely]] {
            SPDLOG_CRITICAL("SIGBUS invalid access of memory at pointer {}",
                            (void*)(faultAddr));
            faabric::util::printStackTrace(contextR);
            ::exit(1);
        }
        range = *rangeIt;
        rangeCache = range;
    }
    faabric::util::FullLock rangeLock{ range->mx };
    if (range->pointerPermissions(faultAddr) == 0) [[unlikely]] {
        SPDLOG_ERROR("[!] UFFD out of bounds access of memory at pointer {} in "
                     "range {}..{}",
                     (void*)(faultAddr),
                     (void*)(range->mapStart),
                     (void*)(range->mapStart + range->mapBytes));
        for (auto it = range->permissions.cbegin();
             it != range->permissions.cend();
             it++) {
            const auto& [regionStart, regionPerm] = *it;
            if (regionStart == 0 || regionStart == UINTPTR_MAX) {
                continue;
            }
            uintptr_t regionEnd = (it + 1)->second;
            if (regionPerm != 0) {
                SPDLOG_ERROR("[!] Registered permission range [{}]: {}..{}",
                             regionPerm,
                             (void*)regionStart,
                             (void*)regionEnd);
            }
        }
        rangeLock.unlock();
        faabric::util::printStackTrace(contextR);
        ::pthread_kill(::pthread_self(), SIGSEGV);
        return;
    }
    size_t faultPageOffset =
      ((size_t(faultAddr) - size_t(range->mapStart)) & ~(FAULT_PAGE_SIZE - 1));

    size_t faultEnd =
      std::min(size_t(faultPageOffset) + FAULT_PAGE_SIZE, range->mapBytes);
    size_t faultSize = faultEnd - faultPageOffset;
    range->touch(faultEnd);
    auto initSource = range->initSource;
    rangeLock.unlock();
    if (faultPageOffset < initSource.size()) {
        ZoneScopedN("copyPages");
        umam.uffd.copyPages(size_t(range->mapStart) + faultPageOffset,
                            faultSize,
                            size_t(initSource.data() + faultPageOffset));
    } else {
        ZoneScopedN("zeroPages");
        umam.uffd.zeroPages(size_t(range->mapStart) + faultPageOffset,
                            faultSize);
    }
}

UffdMemoryArenaManager::UffdMemoryArenaManager()
{
    faabric::util::FullTraceableLock lock{ mx };
    uffd.create(O_CLOEXEC, true);
    struct sigaction action;
    action.sa_flags = SA_RESTART | SA_SIGINFO;
    action.sa_handler = nullptr;
    action.sa_sigaction = &sigbusHandler;
    sigemptyset(&action.sa_mask);
    checkErrno(sigaction(SIGBUS, &action, nullptr),
               "Couldn't register UFFD SIGBUS handler");
    updateRanges(std::make_shared<RangeSet>());
    SPDLOG_INFO("Registered UFFD SIGBUS handler");
}

std::byte* UffdMemoryArenaManager::allocateRange(size_t pages,
                                                 size_t alignmentLog2)
{
    std::shared_ptr range =
      std::make_shared<UffdMemoryRange>(pages, alignmentLog2);
    std::byte* start = range->mapStart;
    {
        faabric::util::FullTraceableLock lock{ mx };

        this->uffd.registerAddressRange(
          size_t(range->mapStart), range->mapBytes, true, false);
        std::shared_ptr newRanges =
          std::make_shared<RangeSet>(*rangesSnapshot());
        newRanges->insert(std::move(range));
        this->updateRanges(std::move(newRanges));
    }
    return start;
}

void UffdMemoryArenaManager::modifyRange(
  std::byte* start,
  std::function<void(UffdMemoryRange&)> action)
{
    auto rangesSnap = this->rangesSnapshot();
    auto rangeIt = rangesSnap->find(start);
    if (rangeIt == rangesSnap->end()) {
        throw std::runtime_error("Invalid pointer for UFFD range action");
    }
    std::shared_ptr range = *rangeIt;
    faabric::util::FullLock rangeLock{ range->mx };
    if (!range->pointerInRange(start)) {
        throw std::runtime_error("Invalid pointer found for UFFD range action");
    }
    action(*range);
}

void UffdMemoryArenaManager::freeRange(std::byte* start)
{
    faabric::util::FullTraceableLock lock{ mx };
    std::shared_ptr newRanges = std::make_shared<RangeSet>(*rangesSnapshot());
    auto rangeIt = newRanges->find(start);
    if (rangeIt == newRanges->end()) {
        throw std::runtime_error("Invalid pointer for UFFD range removal");
    }
    auto& range = *rangeIt;
    faabric::util::FullLock rangeLock{ range->mx };
    size_t mapStart = size_t(range->mapStart);
    size_t mapLength = range->mapBytes;
    uffd.unregisterAddressRange(mapStart, mapLength);
    range->resetPermissions();
    rangeLock.unlock();
    newRanges->erase(rangeIt);
    this->updateRanges(std::move(newRanges));
}

}
