#pragma once

#include <array>
#include <cstdint>
#include <faabric/util/bytes.h>
#include <span>
#include <sstream>
#include <string>
#include <vector>

namespace wasm {

static const char* const BUILTIN_NDP_PUT_FUNCTION = "!builtin_storage_put";
static const char* const BUILTIN_NDP_GET_FUNCTION = "!builtin_storage_get";

class NDPBuiltinModule;

struct BuiltinFunction
{
    const char* name;
    int (*function)(NDPBuiltinModule&, faabric::Message&);
};

extern const std::array<BuiltinFunction, 2> NDP_BUILTINS;

inline const BuiltinFunction& getNdpBuiltin(const std::string& functionName)
{
    auto fn = std::find_if(
      NDP_BUILTINS.cbegin(),
      NDP_BUILTINS.cend(),
      [&](const BuiltinFunction& f) { return f.name == functionName; });
    if (fn == NDP_BUILTINS.cend()) {
        throw std::runtime_error(std::string("Invalid builtin name: ") +
                                 functionName);
    }
    return *fn;
}

struct BuiltinNdpPutArgs
{
    std::vector<uint8_t> key;
    std::vector<uint8_t> value;

    inline std::vector<uint8_t> asBytes() const
    {
        uint64_t keySize = key.size();
        uint64_t valueSize = value.size();
        std::vector<uint8_t> out;
        out.reserve(16 + key.size() + value.size());
        faabric::util::appendBytesOf(out, keySize);
        faabric::util::appendBytesOf(out, valueSize);
        out.insert(out.end(), key.cbegin(), key.cend());
        out.insert(out.end(), value.cbegin(), value.cend());
        return out;
    }

    inline static BuiltinNdpPutArgs fromBytes(std::span<const uint8_t> bytes)
    {
        BuiltinNdpPutArgs out;
        uint64_t keySize{}, valueSize{};
        size_t offset = 0;
        offset = faabric::util::readBytesOf(bytes, offset, &keySize);
        offset = faabric::util::readBytesOf(bytes, offset, &valueSize);
        out.key.assign(bytes.begin() + offset,
                       bytes.begin() + offset + keySize);
        offset += keySize;
        out.value.assign(bytes.begin() + offset,
                         bytes.begin() + offset + valueSize);
        return out;
    }
};

struct BuiltinNdpGetArgs
{
    std::vector<uint8_t> key;
    uint64_t offset, uptoBytes;

    inline std::vector<uint8_t> asBytes() const
    {
        uint64_t keySize = key.size();
        std::vector<uint8_t> out;
        out.reserve(24 + key.size());
        faabric::util::appendBytesOf(out, keySize);
        faabric::util::appendBytesOf(out, offset);
        faabric::util::appendBytesOf(out, uptoBytes);
        out.insert(out.end(), key.cbegin(), key.cend());
        return out;
    }

    inline static BuiltinNdpGetArgs fromBytes(std::span<const uint8_t> bytes)
    {
        BuiltinNdpGetArgs out;
        uint64_t keySize{};
        size_t readOffset = 0;
        readOffset = faabric::util::readBytesOf(bytes, readOffset, &keySize);
        readOffset = faabric::util::readBytesOf(bytes, readOffset, &out.offset);
        readOffset =
          faabric::util::readBytesOf(bytes, readOffset, &out.uptoBytes);
        out.key.assign(bytes.begin() + readOffset,
                       bytes.begin() + readOffset + keySize);
        return out;
    }
};

}
