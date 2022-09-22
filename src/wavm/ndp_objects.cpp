#include "WAVMWasmModule.h"
#include "syscalls.h"

#include <faabric/snapshot/SnapshotRegistry.h>
#include <faabric/state/State.h>
#include <faabric/util/bytes.h>
#include <faabric/util/delta.h>
#include <faabric/util/files.h>
#include <faabric/util/logging.h>
#include <faabric/util/macros.h>
#include <faabric/util/snapshot.h>
#include <faabric/util/timing.h>
#include <faabric/scheduler/ExecutorContext.h>
#include <wasm/WasmExecutionContext.h>
#include <wasm/chaining.h>
#include <wasm/ndp.h>
#include <wavm/NdpBuiltinModule.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string_view>

#include <WAVM/IR/IR.h>
#include <WAVM/Inline/FloatComponents.h>
#include <WAVM/Runtime/Intrinsics.h>
#include <WAVM/Runtime/Runtime.h>

using namespace WAVM;

namespace wasm {

faabric::Message ndpStorageBuiltinCall(const std::string& functionName,
                                       const std::vector<uint8_t>& inputData)
{
    ZoneScopedNS("ndp_objects::ndpStorageBuiltinCall", 6);
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
    ZoneScopedNS("ndp_objects::put", 6);
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
    ZoneScopedNS("ndp_objects::get_mmap", 6);
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
        ZoneScopedN("NDP get chain call");
        int get_call = makeChainedCall(
          BUILTIN_NDP_GET_FUNCTION, 0, nullptr, getArgs.asBytes(), true);
        ZoneNamedN(__zone_await, "NDP get chain call", true);
        get_result = awaitChainedCallMessage(get_call);
    }

    SPDLOG_DEBUG("SB - ndpos_getMmap builtin result: ret {}, {} out bytes",
                 get_result.returnvalue(),
                 get_result.outputdata().size());

    U32 outPtr{ 0 };
    if (get_result.returnvalue() == 0) {
        ZoneScopedN("NDP get map memory");
        const uint8_t* outputData =
          reinterpret_cast<const uint8_t*>(get_result.outputdata().data());
        size_t copyLen =
          std::min(get_result.outputdata().size(), (size_t)maxRequestedLen);
        U32 oldPagesEnd = module_->mmapMemory(copyLen);
        U32 oldPageNumberEnd = oldPagesEnd / WASM_BYTES_PER_PAGE;
        SPDLOG_DEBUG("ndpos_getMmap data start at addr {:08x} page {} len {}",
                     oldPagesEnd,
                     oldPageNumberEnd,
                     copyLen);
        U8* outDataPtr =
          Runtime::memoryArrayPtr<U8>(memoryPtr, oldPagesEnd, (Uptr)copyLen);
        std::copy_n(outputData, copyLen, outDataPtr);
        *outDataLen = copyLen;
        outPtr = oldPagesEnd;
        module_->snapshotExcludedPtrLens.emplace_back(oldPagesEnd, copyLen);
    }

    return outPtr;
}

I32 storageCallAndAwaitImpl(I32 wasmFuncPtr,
                            I32 pyFuncNamePtr,
                            std::span<I32> extraArgs)
{
    ZoneScopedNS("ndp_objects::storageCallAndAwait", 6);
    const faabric::util::SystemConfig& config =
      faabric::util::getSystemConfig();
    const bool isPython = pyFuncNamePtr != 0;
    const std::string pyFuncName =
      isPython ? getStringFromWasm(pyFuncNamePtr) : "";
    SPDLOG_DEBUG(
      "S - __faasmndp_storageCallAndAwait - {} {}", wasmFuncPtr, pyFuncName);

    faabric::Message* call = &faabric::scheduler::ExecutorContext::get()->getMsg();

    WAVMWasmModule* thisModule = static_cast<WAVMWasmModule*>(
      getCurrentWasmExecutionContext()->executingModule);

    // Validate function signature
    if (!isPython) {
        auto* funcInstance = thisModule->getFunctionFromPtr(wasmFuncPtr);
        auto funcType = Runtime::getFunctionType(funcInstance);
        if (funcType.results().size() != 1 ||
            funcType.params().size() != extraArgs.size()) {
            throw std::invalid_argument(
              "Wrong function signature for storageCallAndAwait");
        }
        for (const auto& param : funcType.params()) {
            if (param != IR::ValueType::i32) {
                throw std::invalid_argument("Function argument not i32");
            }
        }
    }

    if (config.isStorageNode || call->forbidndp()) {
        ZoneScopedN("call locally");
        if (isPython) {
            return -0x12345678;
        }
        auto* funcInstance = thisModule->getFunctionFromPtr(wasmFuncPtr);
        auto funcType = Runtime::getFunctionType(funcInstance);
        std::vector<IR::UntaggedValue> funcArgs;
        funcArgs.reserve(extraArgs.size() + 1);
        for (I32 param : extraArgs) {
            funcArgs.push_back(param);
        }
        funcArgs.push_back(0);
        IR::UntaggedValue result;
        Runtime::invokeFunction(thisModule->executionContext,
                                funcInstance,
                                funcType,
                                funcArgs.data(),
                                &result);
        return result.i32;
    } else {
        auto zygoteSnapshotKV = thisModule->getZygoteSnapshot();
        const std::span<const uint8_t> zygoteSnapshot{
            zygoteSnapshotKV->get(), zygoteSnapshotKV->size()
        };
        auto zygoteDelta = thisModule->deltaSnapshot(zygoteSnapshot);

        SPDLOG_INFO("{} - NDP sending snapshot of {} bytes",
                    call->id(),
                    zygoteDelta.size());
        std::vector<int32_t> wasmGlobals = thisModule->getGlobals();
        int ndpCallId = chainNdpCall(zygoteDelta,
                                     call->inputdata(),
                                     wasmFuncPtr,
                                     pyFuncName.c_str(),
                                     extraArgs,
                                     wasmGlobals);
        faabric::Message ndpResult = awaitChainedCallMessage(ndpCallId);

        if (ndpResult.returnvalue() != 0) {
            call->set_outputdata(ndpResult.outputdata());
            SPDLOG_DEBUG("Chained NDP resulted in error code {}",
                         ndpResult.returnvalue());
            return ndpResult.returnvalue();
        }

        {
            // restore globals
            for (int i = 0; i < ndpResult.wasmglobals_size(); i++) {
                int32_t value = ndpResult.wasmglobals(i);
                SPDLOG_DEBUG("Restoring global #{}: new {} <- old {}",
                             i,
                             value,
                             wasmGlobals.at(i));
                thisModule->updateGlobal(i, value);
            }
        }

        SPDLOG_INFO("{} - NDP delta restore from {} bytes",
                    call->id(),
                    ndpResult.outputdata().size());
        // faabric::util::writeBytesToFile(
        //   "/usr/local/faasm/debug_shared_store/debug_delta.bin",
        //   faabric::util::stringToBytes(ndpResult.outputdata()));
        std::span<const uint8_t> memoryDelta =
          std::span(BYTES_CONST(ndpResult.outputdata().data()),
                    ndpResult.outputdata().size());
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
    return storageCallAndAwaitImpl(wasmFuncPtr, 0, {});
}

WAVM_DEFINE_INTRINSIC_FUNCTION(env,
                               "__faasmndp_storageCallAndAwait1",
                               I32,
                               __faasmndp_storageCallAndAwait1,
                               I32 wasmFuncPtr,
                               I32 arg1)
{
    std::array args{ arg1 };
    return storageCallAndAwaitImpl(wasmFuncPtr, 0, args);
}

WAVM_DEFINE_INTRINSIC_FUNCTION(env,
                               "__faasmndp_storageCallAndAwait2",
                               I32,
                               __faasmndp_storageCallAndAwait2,
                               I32 wasmFuncPtr,
                               I32 arg1,
                               I32 arg2)
{
    std::array args{ arg1, arg2 };
    return storageCallAndAwaitImpl(wasmFuncPtr, 0, args);
}

WAVM_DEFINE_INTRINSIC_FUNCTION(env,
                               "__faasmndp_storageCallAndAwait_py",
                               I32,
                               __faasmndp_storageCallAndAwait_py,
                               I32 namePtr)
{
    return storageCallAndAwaitImpl(0, namePtr, {});
}

void ndpLink() {}

}
