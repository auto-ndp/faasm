#pragma once

#include <cstdint>
#include <faabric/proto/faabric.pb.h>
#include <span>
#include <string>
#include <vector>

namespace wasm {

int awaitChainedCall(unsigned int messageId);

int awaitChainedCallOutput(unsigned int messageId,
                           uint8_t* buffer,
                           int bufferLen);

int makeChainedCall(const std::string& functionName,
                    int wasmFuncPtr,
                    const char* pyFunc,
                    const std::vector<uint8_t>& inputData,
                    bool isStorage = false);

faabric::Message awaitChainedCallMessage(unsigned int messageId);

int chainNdpCall(std::span<const uint8_t> zygoteDelta,
                 std::span<const char> inputData,
                 int funcPtr,
                 const char* pyFuncName,
                 std::span<const int32_t> extraArgs,
                 std::span<const int32_t> wasmGlobals);

}
