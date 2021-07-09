#pragma once

#include <cstdint>
#include <faabric/proto/faabric.pb.h>
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

int chainNdpCall(const std::string& zygoteDelta,
                 const std::string& inputData,
                 int funcPtr,
                 const char* pyFuncName);

}
