#include "WAVMWasmModule.h"
#include "syscalls.h"

#include <faabric/scheduler/ExecutorContext.h>
#include <faabric/snapshot/SnapshotRegistry.h>
#include <faabric/state/State.h>
#include <faabric/util/bytes.h>
#include <faabric/util/delta.h>
#include <faabric/util/files.h>
#include <faabric/util/logging.h>
#include <faabric/util/macros.h>
#include <faabric/util/snapshot.h>
#include <faabric/util/timing.h>
#include <stdexcept>
#include <storage/S3Wrapper.h>
#include <wasm/WasmExecutionContext.h>
#include <wasm/chaining.h>
#include <wasm/ndp.h>

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

WAVM_DEFINE_INTRINSIC_FUNCTION(env,
                               "__faasmndp_put",
                               I32,
                               __faasmndp_put,
                               I32 keyPtr,
                               I32 keyLen,
                               I32 dataPtr,
                               I32 dataLen)
{
    ZoneScopedN("__faasmndp_put");
    static storage::S3Wrapper s3w;
    if (keyPtr <= 0 || keyLen <= 0 || dataPtr <= 0 || dataLen <= 0) {
        return -1;
    }
    const faabric::util::SystemConfig& config =
      faabric::util::getSystemConfig();

    WAVMWasmModule* module_ = getExecutingWAVMModule();
    Runtime::Memory* memoryPtr = module_->defaultMemory;
    U8* key =
      Runtime::memoryArrayPtr<U8>(memoryPtr, (Uptr)keyPtr, (Uptr)keyLen);
    std::string keyStr(reinterpret_cast<char*>(key), keyLen);
    U8* data =
      Runtime::memoryArrayPtr<U8>(memoryPtr, (Uptr)dataPtr, (Uptr)dataLen);

    SPDLOG_DEBUG(
      "S - __faasmndp_put - {} {} {} {}", keyPtr, keyLen, dataPtr, dataLen);

    if (config.isStorageNode && false) {
        // TODO: RPC call to CEPH plugin
        // ndpStorageBuiltinCall(BUILTIN_NDP_PUT_FUNCTION, putArgs.asBytes());
    } else {
        try {
            s3w.addKeyBytes(
              module_->getBoundUser(), keyStr, std::span(data, dataLen));
        } catch (const std::runtime_error& err) {
            SPDLOG_ERROR("__faasmndp_put error: {}", err.what());
            return -1;
        }
    }

    return 0;
}

WAVM_DEFINE_INTRINSIC_FUNCTION(env,
                               "__faasmndp_getMmap",
                               I32,
                               __faasmndp_getMmap,
                               I32 keyPtr,
                               I32 keyLen,
                               I64 offset,
                               I64 maxRequestedLen,
                               I32 outDataLenPtr)
{
    static storage::S3Wrapper s3w;
    ZoneScopedN("__faasmndp_getMmap");
    if (keyPtr <= 0 || keyLen <= 0 || maxRequestedLen < 0 ||
        maxRequestedLen > INT32_MAX || offset < 0) {
        return 0;
    }

    const faabric::util::SystemConfig& config =
      faabric::util::getSystemConfig();

    WAVMWasmModule* module_ = static_cast<WAVMWasmModule*>(
      getCurrentWasmExecutionContext()->executingModule);
    Runtime::Memory* memoryPtr = module_->defaultMemory;
    U8* key =
      Runtime::memoryArrayPtr<U8>(memoryPtr, (Uptr)keyPtr, (Uptr)keyLen);
    U32* outDataLen = &Runtime::memoryRef<U32>(memoryPtr, (Uptr)outDataLenPtr);
    *outDataLen = 0;

    SPDLOG_DEBUG("S - __faasmndp_getMmap - {} {} {:x} {}",
                 keyPtr,
                 keyLen,
                 maxRequestedLen,
                 outDataLenPtr);

    faabric::Message get_result;
    std::unique_ptr<WasmExecutionContext> wec;
    if (config.isStorageNode && false) {
        // TODO: RPC call to CEPH plugin
        // get_result =
        //   ndpStorageBuiltinCall(BUILTIN_NDP_GET_FUNCTION, getArgs.asBytes());
    } else {
        U32 oldPagesEnd = 0;
        ssize_t allocLen = 0;
        char* bufferStart = nullptr;
        ssize_t actualLength = 0;
        try {
            actualLength = s3w.getKeyPartIntoBuf(
              module_->getBoundUser(),
              std::string(reinterpret_cast<char*>(key), keyLen),
              offset,
              maxRequestedLen,
              [&](ssize_t len) {
                  oldPagesEnd = module_->mmapMemory(len);
                  allocLen = len;
                  bufferStart =
                    Runtime::memoryArrayPtr<char>(memoryPtr, oldPagesEnd, len);
              },
              [&]() { return bufferStart; });
        } catch (const std::runtime_error& err) {
            SPDLOG_ERROR("__faasmndp_getMmap error: {}", err.what());
            return -1;
        }
        if (bufferStart == nullptr || actualLength == 0) {
            return 0;
        }
        module_->snapshotExcludedPtrLens.emplace_back(oldPagesEnd, allocLen);
        *outDataLen = actualLength;
        return oldPagesEnd;
    }

    return 0;
}

I32 storageCallAndAwaitImpl(I32 keyPtr,
                            I32 keyLen,
                            I32 wasmFuncPtr,
                            I32 pyFuncNamePtr,
                            std::span<I32> extraArgs)
{
    ZoneScopedN("storageCallAndAwaitImpl");
    const faabric::util::SystemConfig& config =
      faabric::util::getSystemConfig();
    const bool isPython = pyFuncNamePtr != 0;
    const std::string pyFuncName =
      isPython ? getStringFromWasm(pyFuncNamePtr) : "";
    SPDLOG_DEBUG("S - storageCallAndAwaitImpl - {} {} #{}",
                 wasmFuncPtr,
                 pyFuncName,
                 extraArgs.size());

    faabric::Message* call =
      &faabric::scheduler::ExecutorContext::get()->getMsg();

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
                               I32 keyPtr,
                               I32 keyLen,
                               I32 wasmFuncPtr)
{
    return storageCallAndAwaitImpl(keyPtr, keyLen, wasmFuncPtr, 0, {});
}

WAVM_DEFINE_INTRINSIC_FUNCTION(env,
                               "__faasmndp_storageCallAndAwait1",
                               I32,
                               __faasmndp_storageCallAndAwait1,
                               I32 keyPtr,
                               I32 keyLen,
                               I32 wasmFuncPtr,
                               I32 arg1)
{
    std::array args{ arg1 };
    return storageCallAndAwaitImpl(keyPtr, keyLen, wasmFuncPtr, 0, args);
}

WAVM_DEFINE_INTRINSIC_FUNCTION(env,
                               "__faasmndp_storageCallAndAwait2",
                               I32,
                               __faasmndp_storageCallAndAwait2,
                               I32 keyPtr,
                               I32 keyLen,
                               I32 wasmFuncPtr,
                               I32 arg1,
                               I32 arg2)
{
    std::array args{ arg1, arg2 };
    return storageCallAndAwaitImpl(keyPtr, keyLen, wasmFuncPtr, 0, args);
}

WAVM_DEFINE_INTRINSIC_FUNCTION(env,
                               "__faasmndp_storageCallAndAwait_py",
                               I32,
                               __faasmndp_storageCallAndAwait_py,
                               I32 keyPtr,
                               I32 keyLen,
                               I32 namePtr)
{
    return storageCallAndAwaitImpl(keyPtr, keyLen, 0, namePtr, {});
}

void ndpLink() {}

}
