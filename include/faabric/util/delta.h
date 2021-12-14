#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <utility>

namespace faabric::util {

struct DeltaSettings
{
    // pages=SIZE;
    bool usePages = true;
    size_t pageSize = 4096;
    // xor;
    bool xorWithOld = true;
    // zstd=LEVEL;
    bool useZstd = true;
    int zstdLevel = 1;

    explicit DeltaSettings(const std::string& definition);
    std::string toString() const;
};

inline constexpr uint8_t DELTA_PROTOCOL_VERSION = 1;

enum DeltaCommand : uint8_t
{
    // followed by u32(total size)
    DELTACMD_TOTAL_SIZE = 0x00,
    // followed by u64(compressed length), u64(decompressed length),
    // bytes(compressed commands)
    DELTACMD_ZSTD_COMPRESSED_COMMANDS = 0x01,
    // followed by u32(offset), u32(length), bytes(data)
    DELTACMD_DELTA_OVERWRITE = 0x02,
    // followed by u32(offset), u32(length), bytes(data)
    DELTACMD_DELTA_XOR = 0x03,
    // final command
    DELTACMD_END = 0xFE,
};

std::vector<uint8_t> serializeDelta(
  const DeltaSettings& cfg,
  std::span<const uint8_t> oldData,
  std::span<const uint8_t> newData,
  std::span<const std::pair<uint32_t, uint32_t>> excludedPtrLens = {});

void applyDelta(std::span<const uint8_t> delta,
                std::function<void(uint32_t)> setDataSize,
                std::function<uint8_t*()> getDataPointer);

}
