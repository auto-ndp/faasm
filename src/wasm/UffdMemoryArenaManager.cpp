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
    constexpr size_t FAULT_PAGE_SIZE = 4096;
    if (code != SIGBUS) [[unlikely]] {
        std::terminate();
    }
    std::byte* faultAddr = (std::byte*)siginfo->si_addr;
    std::byte* faultPage =
      (std::byte*)((size_t)faultAddr & ~(FAULT_PAGE_SIZE - 1));
    UffdMemoryArenaManager& umam = UffdMemoryArenaManager::instance();
    faabric::util::SharedLock lock{ umam.mx };
    auto range = umam.ranges.find(faultAddr);
    if (range == umam.ranges.end()) [[unlikely]] {
        lock.unlock();
        SPDLOG_CRITICAL("SIGBUS invalid access of memory at pointer {}",
                        (void*)(faultAddr));
        faabric::util::printStackTrace(contextR);
        ::exit(1);
    }
    if (range->pointerPermissions(faultAddr) == 0) [[unlikely]] {
        SPDLOG_ERROR(
          "UFFD out of bounds access of memory at pointer {} in range {}..{}",
          (void*)(faultAddr),
          (void*)(range->mapStart),
          (void*)(range->mapStart + range->mapBytes));
        lock.unlock();
        faabric::util::printStackTrace(contextR);
        ::pthread_kill(::pthread_self(), SIGSEGV);
        return;
    }
    size_t offset = faultPage - range->mapStart;
    range->touch(offset + FAULT_PAGE_SIZE);
    auto initSource = range->initSource;
    lock.unlock();
    if (offset < initSource.size()) {
        umam.uffd.copyPages(size_t(faultPage),
                            FAULT_PAGE_SIZE,
                            size_t(initSource.data() + offset));
    } else {
        umam.uffd.zeroPages(size_t(faultPage), FAULT_PAGE_SIZE);
    }
}

UffdMemoryArenaManager::UffdMemoryArenaManager()
{
    faabric::util::FullLock lock{ mx };
    uffd.create(O_CLOEXEC, true);
    struct sigaction action;
    action.sa_flags = SA_RESTART | SA_SIGINFO;
    action.sa_handler = nullptr;
    action.sa_sigaction = &sigbusHandler;
    sigemptyset(&action.sa_mask);
    checkErrno(sigaction(SIGBUS, &action, nullptr),
               "Couldn't register UFFD SIGBUS handler");
    SPDLOG_INFO("Registered UFFD SIGBUS handler");
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

void UffdMemoryArenaManager::modifyRange(
  std::byte* start,
  std::function<void(UffdMemoryRange&)> action)
{
    faabric::util::SharedLock lock{ mx };
    auto range = this->ranges.find(start);
    if (range == this->ranges.end()) {
        throw std::runtime_error("Invalid pointer for UFFD range action");
    }
    if (!range->pointerInRange(start)) {
        throw std::runtime_error("Invalid pointer found for UFFD range action");
    }
    faabric::util::FullLock rangeLock{ range->mx };
    action(*range);
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

}
