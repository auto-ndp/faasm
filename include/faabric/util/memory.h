#pragma once

#include <cstdint>
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

// Keeps track of dirty pages across many memory ranges independently
// Allows only one range per start address, they should not overlap
namespace dirty_tracker {
// Makes sure the memory range starting at `start` and ending at
// `start+byteLength` gets tracked If byteLength == 0, stops tracking that
// memory range New parts of regions will be marked as dirty
void trackMemoryRange(uint8_t* start, size_t byteLength);

void markClean(uint8_t* rangeStart, size_t substartOffset, size_t sublength);

// Gets all the dirty subregions (as pairs of (start, byte length)) for a given
// memory region
std::vector<std::pair<uint8_t*, size_t>> getDirtySubregions(uint8_t* start);
}

}
