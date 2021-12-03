#include <faabric/util/logging.h>
#include <faabric/util/memory.h>
#include <faabric/util/timing.h>

#include <fcntl.h>
#include <shared_mutex>
#include <stdexcept>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>

#define CLEAR_REFS "/proc/self/clear_refs"
#define PAGEMAP "/proc/self/pagemap"

#define PAGEMAP_ENTRY_BYTES 8
#define PAGEMAP_SOFT_DIRTY (1Ull << 55)

namespace faabric::util {

// -------------------------
// Alignment
// -------------------------

bool isPageAligned(void* ptr)
{
    return (((uintptr_t)(const void*)(ptr)) % (HOST_PAGE_SIZE) == 0);
}

size_t getRequiredHostPages(size_t nBytes)
{
    // Rounding up
    size_t nHostPages = (nBytes + faabric::util::HOST_PAGE_SIZE - 1) /
                        faabric::util::HOST_PAGE_SIZE;
    return nHostPages;
}

size_t getRequiredHostPagesRoundDown(size_t nBytes)
{
    // Relying on integer division rounding down
    size_t nHostPages = nBytes / faabric::util::HOST_PAGE_SIZE;
    return nHostPages;
}

size_t alignOffsetDown(size_t offset)
{
    size_t nHostPages = getRequiredHostPagesRoundDown(offset);
    return nHostPages * faabric::util::HOST_PAGE_SIZE;
}

AlignedChunk getPageAlignedChunk(long offset, long length)
{
    // Calculate the page boundaries
    auto nPagesOffset =
      (long)faabric::util::getRequiredHostPagesRoundDown(offset);
    auto nPagesUpper =
      (long)faabric::util::getRequiredHostPages(offset + length);
    long nPagesLength = nPagesUpper - nPagesOffset;

    long nBytesLength = nPagesLength * faabric::util::HOST_PAGE_SIZE;

    long nBytesOffset = nPagesOffset * faabric::util::HOST_PAGE_SIZE;

    // Note - this value is the offset from the base of the new region
    long shiftedOffset = offset - nBytesOffset;

    AlignedChunk c{
        .originalOffset = offset,
        .originalLength = length,
        .nBytesOffset = nBytesOffset,
        .nBytesLength = nBytesLength,
        .nPagesOffset = nPagesOffset,
        .nPagesLength = nPagesLength,
        .offsetRemainder = shiftedOffset,
    };

    return c;
}

// -------------------------
// Dirty page tracking
// -------------------------

void resetDirtyTracking()
{
    SPDLOG_TRACE("Resetting dirty tracking");

    FILE* fd = fopen(CLEAR_REFS, "w");
    if (fd == nullptr) {
        SPDLOG_ERROR("Could not open clear_refs ({})", strerror(errno));
        throw std::runtime_error("Could not open clear_refs");
    }

    // Write 4 to the file to track from now on
    // https://www.kernel.org/doc/html/v5.4/admin-guide/mm/soft-dirty.html
    char value[] = "4";
    size_t nWritten = fwrite(value, sizeof(char), 1, fd);
    if (nWritten != 1) {
        SPDLOG_ERROR("Failed to write to clear_refs ({})", nWritten);
        throw std::runtime_error("Failed to write to clear_refs");
    }

    fclose(fd);
}

void readPagemapEntries(uintptr_t ptr,
                        int nEntries,
                        std::vector<uint64_t>& entries)
{
    // Work out offset for this pointer in the pagemap
    off_t offset = (ptr / getpagesize()) * PAGEMAP_ENTRY_BYTES;

    // Open the pagemap
    FILE* fd = fopen(PAGEMAP, "rb");
    if (fd == nullptr) {
        SPDLOG_ERROR("Could not open pagemap ({})", strerror(errno));
        throw std::runtime_error("Could not open pagemap");
    }

    // Skip to location of this page
    int r = fseek(fd, offset, SEEK_SET);
    if (r < 0) {
        SPDLOG_ERROR("Could not seek pagemap ({})", r);
        throw std::runtime_error("Could not seek pagemap");
    }

    // Read the entries
    entries.assign(nEntries, 0);
    int nRead = fread(entries.data(), PAGEMAP_ENTRY_BYTES, nEntries, fd);
    if (nRead != nEntries) {
        SPDLOG_ERROR("Could not read pagemap ({} != {})", nRead, nEntries);
        throw std::runtime_error("Could not read pagemap");
    }

    fclose(fd);
}

std::vector<int> getDirtyPageNumbers(const uint8_t* ptr, int nPages)
{
    uintptr_t vptr = (uintptr_t)ptr;

    // Get the pagemap entries
    std::vector<uint64_t> entries;
    readPagemapEntries(vptr, nPages, entries);

    // Iterate through to get boolean flags
    std::vector<int> pageNumbers;
    for (int i = 0; i < nPages; i++) {
        if (entries.at(i) & PAGEMAP_SOFT_DIRTY) {
            pageNumbers.emplace_back(i);
        }
    }

    return pageNumbers;
}

std::vector<std::pair<uint32_t, uint32_t>> getDirtyRegions(const uint8_t* ptr,
                                                           int nPages)
{
    std::vector<int> dirtyPages = getDirtyPageNumbers(ptr, nPages);

    // Add a new region for each page, unless the one before it was also dirty,
    // in which case we merge them
    std::vector<std::pair<uint32_t, uint32_t>> regions;
    for (int p = 0; p < dirtyPages.size(); p++) {
        int thisPageNum = dirtyPages.at(p);
        uint32_t thisPageStart = thisPageNum * HOST_PAGE_SIZE;
        uint32_t thisPageEnd = thisPageStart + HOST_PAGE_SIZE;

        if (p > 0 && dirtyPages.at(p - 1) == thisPageNum - 1) {
            // Previous page was also dirty, just update last region
            regions.back().second = thisPageEnd;
        } else {
            // Previous page wasn't dirty, add new region
            regions.emplace_back(thisPageStart, thisPageEnd);
        }
    }

    return regions;
}

// UserfaultFd wrapper
std::pair<int, uffdio_api> UserfaultFd::release()
{
    int oldFd = fd;
    uffdio_api oldApi = api;
    fd = -1;
    api = {};
    return std::make_pair(oldFd, oldApi);
}

void UserfaultFd::create(int flags, bool sigbus)
{
    clear();
    int result = syscall(SYS_userfaultfd, flags);
    if (result < 0) {
        errno = -result;
        perror("Error creating userfaultfd");
        throw std::runtime_error("Couldn't create userfaultfd");
    }
    fd = result;
    api.api = UFFD_API;
    api.features = UFFD_FEATURE_THREAD_ID;
    if (sigbus) {
        api.features |= UFFD_FEATURE_SIGBUS;
    }
    api.ioctls = 0;
    result = ioctl(fd, UFFDIO_API, &api);
    if (result < 0) {
        throw std::runtime_error("Couldn't handshake userfaultfd api");
    }
}

void UserfaultFd::register_address_range(size_t startPtr,
                                         size_t length,
                                         bool modeMissing,
                                         bool modeWriteProtect)
{
    checkFd();
    uffdio_register r = {};
    if (!(modeMissing || modeWriteProtect)) {
        throw std::invalid_argument(
          "UFFD register call must have at least one mode enabled");
    }
    if (modeMissing) {
        r.mode |= UFFDIO_REGISTER_MODE_MISSING;
    }
    if (modeWriteProtect) {
        if ((api.features & UFFD_FEATURE_PAGEFAULT_FLAG_WP) == 0) {
            throw std::runtime_error("WriteProtect mode on UFFD not supported");
        }
        r.mode |= UFFDIO_REGISTER_MODE_WP;
    }
    r.range.start = startPtr;
    r.range.len = length;
    if (ioctl(fd, UFFDIO_REGISTER, &r) < 0) {
        perror("UFFDIO_REGISTER error");
        throw std::runtime_error(
          "Couldn't register an address range with UFFD");
    }
}

void UserfaultFd::unregister_address_range(size_t startPtr, size_t length)
{
    checkFd();
    uffdio_range r = {};
    r.start = startPtr;
    r.len = length;
    if (ioctl(fd, UFFDIO_UNREGISTER, &r) < 0) {
        perror("UFFDIO_UNREGISTER error");
        throw std::runtime_error(
          "Couldn't unregister an address range from UFFD");
    }
}

std::optional<uffd_msg> UserfaultFd::readEvent()
{
    checkFd();
    uffd_msg ev;
retry:
    int result = read(fd, (void*)&ev, sizeof(uffd_msg));
    if (result < 0) {
        if (errno == EAGAIN) {
            goto retry;
        } else if (errno == EWOULDBLOCK) {
            return std::nullopt;
        } else {
            perror("read from UFFD error");
            throw std::runtime_error("Error reading from the UFFD");
        }
    }
    return ev;
}
void UserfaultFd::writeProtectPages(size_t startPtr,
                                    size_t length,
                                    bool preventWrites,
                                    bool dontWake)
{
    checkFd();
    if ((api.features & UFFD_FEATURE_PAGEFAULT_FLAG_WP) == 0) {
        throw std::runtime_error("Write-protect pages not supported by "
                                 "UFFD on this kernel version");
    }
    uffdio_writeprotect wp = {};
    if (preventWrites) {
        wp.mode |= UFFDIO_WRITEPROTECT_MODE_WP;
    }
    if (dontWake) {
        wp.mode |= UFFDIO_WRITEPROTECT_MODE_DONTWAKE;
    }
    wp.range.start = startPtr;
    wp.range.len = length;
retry:
    if (ioctl(fd, UFFDIO_WRITEPROTECT, &wp) < 0) {
        if (errno == EAGAIN) {
            goto retry;
        }
        perror("UFFDIO_WRITEPROTECT error");
        throw std::runtime_error(
          "Couldn't write-protect-modify an address range through UFFD");
    }
}

void UserfaultFd::zeroPages(size_t startPtr, size_t length, bool dontWake)
{
    checkFd();
    uffdio_zeropage zp = {};
    if (dontWake) {
        zp.mode |= UFFDIO_ZEROPAGE_MODE_DONTWAKE;
    }
    zp.range.start = startPtr;
    zp.range.len = length;
retry:
    if (ioctl(fd, UFFDIO_ZEROPAGE, &zp) < 0) {
        if (errno == EAGAIN) {
            goto retry;
        }
        perror("UFFDIO_ZEROPAGE error");
        throw std::runtime_error(
          "Couldn't zero-page an address range through UFFD");
    }
}

void UserfaultFd::copyPages(size_t targetStartPtr,
                            size_t length,
                            size_t sourceStartPtr,
                            bool writeProtect,
                            bool dontWake)
{
    checkFd();
    uffdio_copy cp = {};
    if (dontWake) {
        cp.mode |= UFFDIO_COPY_MODE_DONTWAKE;
    }
    if (writeProtect) {
        cp.mode |= UFFDIO_COPY_MODE_WP;
    }
    cp.src = sourceStartPtr;
    cp.len = length;
    cp.dst = targetStartPtr;
retry:
    if (ioctl(fd, UFFDIO_COPY, &cp) < 0) {
        if (errno == EAGAIN) {
            goto retry;
        }
        perror("UFFDIO_COPY error");
        throw std::runtime_error("Couldn't copy an address range through UFFD");
    }
}

void UserfaultFd::wakePages(size_t startPtr, size_t length)
{
    checkFd();
    uffdio_range wr = {};
    wr.start = startPtr;
    wr.len = length;
retry:
    if (ioctl(fd, UFFDIO_WAKE, &wr) < 0) {
        if (errno == EAGAIN) {
            goto retry;
        }
        perror("UFFDIO_WAKE error");
        throw std::runtime_error("Couldn't wake an address range through UFFD");
    }
}

}
