#pragma once

#include <cstdint>
#include <fcntl.h>
#include <linux/userfaultfd.h>
#include <optional>
#include <stdexcept>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <vector>

namespace faabric::util {

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

bool isPageAligned(void* ptr);

size_t getRequiredHostPages(size_t nBytes);

size_t getRequiredHostPagesRoundDown(size_t nBytes);

size_t alignOffsetDown(size_t offset);

AlignedChunk getPageAlignedChunk(long offset, long length);

// -------------------------
// Dirty pages
// -------------------------
void resetDirtyTracking();

std::vector<int> getDirtyPageNumbers(const uint8_t* ptr, int nPages);

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
    void register_address_range(size_t startPtr,
                                size_t length,
                                bool modeMissing,
                                bool modeWriteProtect);

    // Thread-safe
    void unregister_address_range(size_t startPtr, size_t length);

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

}
