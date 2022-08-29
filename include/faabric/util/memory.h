#pragma once

#include <cstdint>
#include <fcntl.h>
#include <functional>
#include <linux/userfaultfd.h>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <vector>

namespace faabric::util {

/*
 * Merges all the dirty page flags from the list of vectors into the first
 * vector in place.
 */
void mergeManyDirtyPages(std::vector<char>& dest,
                         const std::vector<std::vector<char>>& source);

/*
 * Merges the dirty page flags from the source into the destination.
 */
void mergeDirtyPages(std::vector<char>& dest, const std::vector<char>& source);

/*
 * Typedef used to enforce RAII on mmapped memory regions
 */
typedef std::unique_ptr<uint8_t[], std::function<void(uint8_t*)>> MemoryRegion;

// -------------------------
// Alignment
// -------------------------
struct AlignedChunk
{
    long originalOffset = 0;
    long originalLength = 0;
    long nBytesOffset = 0;
    long nBytesLength = 0;
    long nPagesOffset = 0;
    long nPagesLength = 0;
    long offsetRemainder = 0;
};

static const long HOST_PAGE_SIZE = sysconf(_SC_PAGESIZE);

bool isPageAligned(const void* ptr);

size_t getRequiredHostPages(size_t nBytes);

size_t getRequiredHostPagesRoundDown(size_t nBytes);

size_t alignOffsetDown(size_t offset);

AlignedChunk getPageAlignedChunk(long offset, long length);

// -------------------------
// Dirty pages
// -------------------------
void resetDirtyTracking();

std::vector<int> getDirtyPageNumbers(const uint8_t* ptr, int nPages);

std::vector<std::pair<uint32_t, uint32_t>> getDirtyRegions(const uint8_t* ptr,
                                                           int nPages);

struct UserfaultFd
{
    int fd = -1;
    uffdio_api api = {};

    // Only allow moving, not copying - owns the fd
    inline UserfaultFd() {}
    inline ~UserfaultFd() { clear(); }
    UserfaultFd(const UserfaultFd&) = delete;
    inline UserfaultFd(UserfaultFd&& other)
    {
        this->operator=(std::move(other));
    }
    UserfaultFd& operator=(const UserfaultFd&) = delete;
    inline UserfaultFd& operator=(UserfaultFd&& other)
    {
        fd = other.fd;
        api = other.api;
        other.fd = -1;
        other.api = {};
        return *this;
    }

    // Close fd if present
    inline void clear()
    {
        if (fd >= 0) {
            close(fd);
            fd = -1;
            api = {};
        }
    }

    // Release ownership and return the fd
    std::pair<int, uffdio_api> release();

    void create(int flags = 0, bool sigbus = false);

    // Thread-safe
    inline void checkFd()
    {
        if (fd < 0) {
            throw std::runtime_error("UFFD fd not initialized");
        }
    }

    // Write-protect mode requires at least Linux 5.7 kernel
    // Thread-safe
    void registerAddressRange(size_t startPtr,
                              size_t length,
                              bool modeMissing,
                              bool modeWriteProtect);

    // Thread-safe
    void unregisterAddressRange(size_t startPtr, size_t length);

    // Thread-safe
    std::optional<uffd_msg> readEvent();

    // Thread-safe
    void writeProtectPages(size_t startPtr,
                           size_t length,
                           bool preventWrites = true,
                           bool dontWake = false);

    // Thread-safe
    void zeroPages(size_t startPtr, size_t length, bool dontWake = false);

    // Thread-safe
    void copyPages(size_t targetStartPtr,
                   size_t length,
                   size_t sourceStartPtr,
                   bool writeProtect = false,
                   bool dontWake = false);

    // Thread-safe
    void wakePages(size_t startPtr, size_t length);
};

// -------------------------
// Allocation
// -------------------------

MemoryRegion allocatePrivateMemory(size_t size);

MemoryRegion allocateSharedMemory(size_t size);

MemoryRegion allocateVirtualMemory(size_t size);

void claimVirtualMemory(std::span<uint8_t> region);

void mapMemoryPrivate(std::span<uint8_t> target, int fd);

void mapMemoryShared(std::span<uint8_t> target, int fd);

void resizeFd(int fd, size_t size);

void writeToFd(int fd, off_t offset, std::span<const uint8_t> data);

int createFd(size_t size, const std::string& fdLabel);

void appendDataToFd(int fd, std::span<uint8_t> data);
}
