#include "WAVMWasmModule.h"
#include "syscalls.h"

#include <faabric/snapshot/SnapshotRegistry.h>
#include <faabric/state/State.h>
#include <faabric/util/bytes.h>
#include <faabric/util/delta.h>
#include <faabric/util/files.h>
#include <faabric/util/logging.h>
#include <faabric/util/snapshot.h>
#include <wasm/WasmExecutionContext.h>
#include <wasm/chaining.h>
#include <wasm/ndp.h>
#include <wavm/NdpBuiltinModule.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>

#include <WAVM/IR/IR.h>
#include <WAVM/Inline/FloatComponents.h>
#include <WAVM/Runtime/Intrinsics.h>
#include <WAVM/Runtime/Runtime.h>

using namespace WAVM;

namespace wasm {

faabric::Message ndpStorageBuiltinCall(std::string functionName,
                                       const std::vector<uint8_t>& inputData)
{
    auto wee = getCurrentWasmExecutionContext();
    faabric::Message msg = faabric::util::messageFactory(
      wee->executingModule->getBoundUser(), functionName);
    msg.set_inputdata(inputData.data(), inputData.size());
    msg.set_isstorage(true);
    NDPBuiltinModule mod{};
    mod.bindToFunction(msg);
    mod.executeFunction(msg);
    return msg;
}

WAVM_DEFINE_INTRINSIC_FUNCTION(env,
                               "__faasmndp_put",
                               I32,
                               __faasmndp_put,
                               I32 keyPtr,
                               I32 keyLen,
                               I32 dataPtr,
                               I32 dataLen)
{
    const faabric::util::SystemConfig& config =
      faabric::util::getSystemConfig();

    auto module_ = getExecutingWAVMModule();
    Runtime::Memory* memoryPtr = module_->defaultMemory;
    U8* key =
      Runtime::memoryArrayPtr<U8>(memoryPtr, (Uptr)keyPtr, (Uptr)keyLen);
    U8* data =
      Runtime::memoryArrayPtr<U8>(memoryPtr, (Uptr)dataPtr, (Uptr)dataLen);

    SPDLOG_DEBUG(
      "S - ndpos_put - {} {} {} {}", keyPtr, keyLen, dataPtr, dataLen);

    BuiltinNdpPutArgs putArgs{};
    putArgs.key = std::vector(key, key + keyLen);
    putArgs.value = std::vector(data, data + dataLen);
    faabric::Message put_result;
    if (config.isStorageNode) {
        ndpStorageBuiltinCall(BUILTIN_NDP_PUT_FUNCTION, putArgs.asBytes());
    } else {
        int put_call = makeChainedCall(
          BUILTIN_NDP_PUT_FUNCTION, 0, nullptr, putArgs.asBytes(), true);
        awaitChainedCallMessage(put_call);
    }

    SPDLOG_DEBUG("SB - ndpos_put builtin result: ret {}, {} out bytes",
                 put_result.returnvalue(),
                 put_result.outputdata().size());

    return put_result.returnvalue();
}

WAVM_DEFINE_INTRINSIC_FUNCTION(env,
                               "__faasmndp_getMmap",
                               I32,
                               __faasmndp_getMmap,
                               I32 keyPtr,
                               I32 keyLen,
                               U32 maxRequestedLen,
                               I32 outDataLenPtr)
{
    const faabric::util::SystemConfig& config =
      faabric::util::getSystemConfig();

    WAVMWasmModule* module_ = static_cast<WAVMWasmModule*>(
      getCurrentWasmExecutionContext()->executingModule);
    Runtime::Memory* memoryPtr = module_->defaultMemory;
    U8* key =
      Runtime::memoryArrayPtr<U8>(memoryPtr, (Uptr)keyPtr, (Uptr)keyLen);
    U32* outDataLen = &Runtime::memoryRef<U32>(memoryPtr, (Uptr)outDataLenPtr);
    *outDataLen = 0;

    SPDLOG_DEBUG("S - ndpos_getMmap - {} {} {:x} {}",
                 keyPtr,
                 keyLen,
                 maxRequestedLen,
                 outDataLenPtr);

    BuiltinNdpGetArgs getArgs{};
    getArgs.key = std::vector(key, key + keyLen);
    getArgs.offset = 0;
    getArgs.uptoBytes = maxRequestedLen;
    faabric::Message get_result;
    std::unique_ptr<WasmExecutionContext> wec;
    if (config.isStorageNode) {
        get_result =
          ndpStorageBuiltinCall(BUILTIN_NDP_GET_FUNCTION, getArgs.asBytes());
    } else {
        int get_call = makeChainedCall(
          BUILTIN_NDP_GET_FUNCTION, 0, nullptr, getArgs.asBytes(), true);
        get_result = awaitChainedCallMessage(get_call);
    }

    SPDLOG_DEBUG("SB - ndpos_getMmap builtin result: ret {}, {} out bytes",
                 get_result.returnvalue(),
                 get_result.outputdata().size());

    U32 outPtr{ 0 };
    if (get_result.returnvalue() == 0) {
        std::vector<uint8_t> outputData =
          faabric::util::stringToBytes(get_result.outputdata());
        size_t copyLen = std::min(outputData.size(), (size_t)maxRequestedLen);
        U32 oldPagesEnd = module_->mmapMemory(copyLen);
        U32 oldPageNumberEnd = oldPagesEnd / WASM_BYTES_PER_PAGE;
        SPDLOG_DEBUG("ndpos_getMmap data start at addr {:08x} page {} len {}",
                     oldPagesEnd,
                     oldPageNumberEnd,
                     copyLen);
        U8* outDataPtr =
          Runtime::memoryArrayPtr<U8>(memoryPtr, oldPagesEnd, (Uptr)copyLen);
        std::copy_n(outputData.begin(), copyLen, outDataPtr);
        *outDataLen = copyLen;
        outPtr = oldPagesEnd;
        module_->snapshotExcludedPtrLens.push_back(
          std::make_pair(oldPagesEnd, copyLen));
    }

    return outPtr;
}

I32 storageCallAndAwaitImpl(I32 wasmFuncPtr, I32 pyFuncNamePtr)
{
    const faabric::util::SystemConfig& config =
      faabric::util::getSystemConfig();
    const bool isPython = pyFuncNamePtr != 0;
    const std::string pyFuncName =
      isPython ? getStringFromWasm(pyFuncNamePtr) : "";
    SPDLOG_DEBUG(
      "S - __faasmndp_storageCallAndAwait - {} {}", wasmFuncPtr, pyFuncName);

    faabric::Message* call = getCurrentWasmExecutionContext()->executingCall;

    WAVMWasmModule* thisModule = static_cast<WAVMWasmModule*>(
      getCurrentWasmExecutionContext()->executingModule);

    if (config.isStorageNode) {
        if (isPython) {
            return -0x12345678;
        }
        auto funcInstance = thisModule->getFunctionFromPtr(wasmFuncPtr);
        auto funcType = Runtime::getFunctionType(funcInstance);
        if (funcType.results().size() != 1 || funcType.params().size() != 0) {
            throw std::invalid_argument(
              "Wrong function signature for storageCallAndAwait");
        }
        std::vector<IR::UntaggedValue> funcArgs = { 0 };
        IR::UntaggedValue result;
        Runtime::invokeFunction(thisModule->executionContext,
                                funcInstance,
                                funcType,
                                funcArgs.data(),
                                &result);
        return result.i32;
    } else {
        auto zygoteSnapshotKV = thisModule->getZygoteSnapshot();
        auto zygoteSnapshot = zygoteSnapshotKV->get();
        faabric::util::SnapshotData zygoteSnap;
        zygoteSnap.fd = -1;
        zygoteSnap.data = zygoteSnapshot;
        zygoteSnap.size = zygoteSnapshotKV->size();
        auto zygoteDelta = faabric::util::bytesToString(thisModule->deltaSnapshot(zygoteSnap));

        SPDLOG_DEBUG("NDP sending snapshot of {} bytes",
                     zygoteDelta.size());
        int ndpCallId = chainNdpCall(
          zygoteDelta, call->inputdata(), wasmFuncPtr, pyFuncName.c_str());
        faabric::Message ndpResult = awaitChainedCallMessage(ndpCallId);

        if (ndpResult.returnvalue() != 0) {
            call->set_outputdata(ndpResult.outputdata());
            SPDLOG_DEBUG("Chained NDP resulted in error code {}",
                         ndpResult.returnvalue());
            return ndpResult.returnvalue();
        }

        SPDLOG_DEBUG("NDP delta restore from {} bytes",
                     ndpResult.outputdata().size());
        // faabric::util::writeBytesToFile(
        //   "/usr/local/faasm/debug_shared_store/debug_delta.bin",
        //   faabric::util::stringToBytes(ndpResult.outputdata()));
        std::vector<uint8_t> memoryDelta =
          faabric::util::stringToBytes(ndpResult.outputdata());
        thisModule->deltaRestore(memoryDelta);
    }

    return 0;
}

WAVM_DEFINE_INTRINSIC_FUNCTION(env,
                               "__faasmndp_storageCallAndAwait",
                               I32,
                               __faasmndp_storageCallAndAwait,
                               I32 wasmFuncPtr)
{
    return storageCallAndAwaitImpl(wasmFuncPtr, 0);
}

WAVM_DEFINE_INTRINSIC_FUNCTION(env,
                               "__faasmndp_storageCallAndAwait_py",
                               I32,
                               __faasmndp_storageCallAndAwait_py,
                               I32 namePtr)
{
    return storageCallAndAwaitImpl(0, namePtr);
}

void ndpLink() {}

}
