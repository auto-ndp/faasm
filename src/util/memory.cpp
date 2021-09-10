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

namespace dirty_tracker {
constexpr uint64_t WASM_MEMORY_MAX_SIZE_BYTES = 4 * 1024 * 1024 * 1024;
constexpr uint64_t TRACKER_PAGE_SIZE = 4096;
constexpr uint64_t TRACKER_MAX_SIZE_BYTES =
  WASM_MEMORY_MAX_SIZE_BYTES / TRACKER_PAGE_SIZE / 8;
struct MemoryRange
{
    MemoryRange(uint8_t* start, size_t byteLength)
      : start(start)
      , byteLength(byteLength)
    {
        dirtyBitmap = (uint8_t*)mmap(nullptr,
                                     TRACKER_MAX_SIZE_BYTES,
                                     PROT_READ | PROT_WRITE,
                                     MAP_PRIVATE | MAP_ANONYMOUS,
                                     -1,
                                     0);
        if (dirtyBitmap == nullptr) {
            SPDLOG_ERROR("Could not map dirty tracker memory range");
            throw std::runtime_error(
              "Could not map dirty tracker memory range");
        }
        madvise(dirtyBitmap, TRACKER_MAX_SIZE_BYTES, MADV_HUGEPAGE);
        std::fill_n(
          dirtyBitmap, byteLength / TRACKER_PAGE_SIZE / 8, uint8_t(0xFF));
    }
    MemoryRange(const MemoryRange&) = delete;
    MemoryRange& operator=(const MemoryRange&) = delete;
    ~MemoryRange()
    {
        if (dirtyBitmap != nullptr) {
            munmap(dirtyBitmap, TRACKER_MAX_SIZE_BYTES);
            dirtyBitmap = nullptr;
            start = nullptr;
            byteLength = 0;
        }
    }
    void setBitmapRange(const size_t startOffsetBytes,
                        const size_t lengthBytes,
                        const bool dirty)
    {
        const size_t startOffsetPage = startOffsetBytes / TRACKER_PAGE_SIZE;
        const size_t endOffsetPage =
          (startOffsetBytes + lengthBytes + TRACKER_PAGE_SIZE - 1) /
          TRACKER_PAGE_SIZE;
        const size_t numSubPages = endOffsetPage - startOffsetPage + 1;
        // first byte of the bitmap
        const size_t firstBytePagesEnd =
          startOffsetPage + (8 - (startOffsetPage % 8));
        for (size_t pageIdx = startOffsetPage; pageIdx < firstBytePagesEnd;
             pageIdx++) {
            if (dirty) {
                dirtyBitmap[pageIdx / 8] |= 1 << (pageIdx % 8);
            } else {
                dirtyBitmap[pageIdx / 8] &= ~uint8_t(1 << (pageIdx % 8));
            }
        }
        // bulk set
        std::fill_n(dirtyBitmap, 1, uint8_t(dirty ? 0xFF : 0x00));
        // last byte
        const size_t lastBytePagesStart = endOffsetPage - (endOffsetPage % 8);
        for (size_t pageIdx = lastBytePagesStart; pageIdx < endOffsetPage;
             pageIdx++) {
            if (dirty) {
                dirtyBitmap[pageIdx / 8] |= 1 << (pageIdx % 8);
            } else {
                dirtyBitmap[pageIdx / 8] &= ~uint8_t(1 << (pageIdx % 8));
            }
        }
    }
    uint8_t* start = nullptr;
    size_t byteLength = 0;
    uint8_t* dirtyBitmap = nullptr;
};

std::shared_mutex memoryRangesMx;
std::vector<MemoryRange> memoryRanges;

void trackMemoryRange(uint8_t* start, size_t byteLength)
{
    ZoneScopedNS("trackMemoryRange", 6);
    ZoneValue(byteLength);
    std::unique_lock _lock(memoryRangesMx);
    memoryRanges.emplace_back(start, byteLength);
}

void markClean(uint8_t* rangeStart, size_t substartOffset, size_t sublength)
{
    ZoneScopedNS("markClean", 6);
    ZoneValue(sublength);
    std::unique_lock _lock(memoryRangesMx);
    std::vector<uint64_t> pagemapEntries;
    for (auto& range : memoryRanges) {
        readPagemapEntries((uintptr_t)range.start,
                           range.byteLength / TRACKER_PAGE_SIZE,
                           pagemapEntries);
        //
    }
}

std::vector<std::pair<uint8_t*, size_t>> getDirtySubregions(uint8_t* start)
{
    ZoneScopedNS("getDirtySubregions", 6);
    std::vector<std::pair<uint8_t*, size_t>> out;
    out.reserve(16);
    std::shared_lock _lock(memoryRangesMx);
    auto memoryRange =
      std::find_if(memoryRanges.cbegin(),
                   memoryRanges.cend(),
                   [start](const MemoryRange& r) { return r.start == start; });
    if (memoryRange == memoryRanges.cend()) {
        throw std::runtime_error(
          "Requested dirty subregions for an untracked region");
    }
    std::vector<uint64_t> pagemap;
    readPagemapEntries((uintptr_t)start,
                       (memoryRange->byteLength + TRACKER_PAGE_SIZE - 1) /
                         TRACKER_PAGE_SIZE,
                       pagemap);
    bool lastDirty = false;
    for (uint64_t pageOffset = 0;
         pageOffset < memoryRange->byteLength / TRACKER_PAGE_SIZE;
         pageOffset++) {
        bool pmDirty = (pagemap.at(pageOffset) & PAGEMAP_SOFT_DIRTY) != 0;
        bool mrDirty =
          memoryRange->dirtyBitmap[pageOffset / 8] & (1 << (pageOffset % 8));
        if (pmDirty || mrDirty) {
            if (lastDirty) {
                out.back().second += TRACKER_PAGE_SIZE;
            } else {
                out.emplace_back(start + (pageOffset * TRACKER_PAGE_SIZE),
                                 TRACKER_PAGE_SIZE);
                lastDirty = true;
            }
        } else {
            if (lastDirty) {
                lastDirty = false;
            }
        }
    }
    ZoneValue(out.size());
    return out;
}
}

}
